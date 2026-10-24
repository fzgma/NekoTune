#include <nekotune/sdk/extension_plugin.h>
#include <QDateTime>
#include <QObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <cmath>

using namespace nekotune;

namespace {
QByteArray makeAudio(int frequency) {
    constexpr int sampleRate = 22050, seconds = 8, samples = sampleRate * seconds;
    QByteArray audio(44 + samples * 2, Qt::Uninitialized);
    auto *data = reinterpret_cast<uchar *>(audio.data());
    const auto u32 = [&](int at, quint32 value) { qToLittleEndian(value, data + at); };
    const auto u16 = [&](int at, quint16 value) { qToLittleEndian(value, data + at); };
    std::memcpy(data, "RIFF", 4);
    u32(4, quint32(audio.size() - 8));
    std::memcpy(data + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, 1);
    u32(24, sampleRate);
    u32(28, sampleRate * 2);
    u16(32, 2);
    u16(34, 16);
    std::memcpy(data + 36, "data", 4);
    u32(40, samples * 2);
    for (int i = 0; i < samples; ++i)
        qToLittleEndian(qint16(std::lround(std::sin(2 * M_PI * frequency * i / sampleRate) * 1800)), data + 44 + i * 2);
    return audio;
}

/// Serves two generated WAV files over loopback HTTP with Range support. Requests without the
/// example header are refused, which proves the host proxy forwards provider headers.
class ToneServer final : public QTcpServer {
  public:
    QByteArray tones[2] = {makeAudio(220), makeAudio(330)};

  protected:
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
            if (!socket->canReadLine() || !socket->peek(16 * 1024).contains("\r\n\r\n"))
                return;
            const auto head = socket->readAll();
            const auto lines = head.split('\n');
            const auto request = lines.value(0).split(' ');
            QByteArray range, example;
            for (const auto &line : lines) {
                const auto lower = line.trimmed().toLower();
                if (lower.startsWith("range:"))
                    range = line.mid(6).trimmed();
                if (lower.startsWith("x-nekotune-example:"))
                    example = line.mid(19).trimmed();
            }
            reply(socket, request.value(0), request.value(1), range, example);
        });
    }

  private:
    void reply(QTcpSocket *socket, const QByteArray &method, const QByteArray &path, const QByteArray &range,
               const QByteArray &example) {
        const auto finish = [socket](const QByteArray &head, const QByteArray &body = {}) {
            socket->write(head + "Connection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        };
        if (example != "audio-test") {
            finish("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n");
            return;
        }
        const auto &buffer = tones[path == "/two.wav" ? 1 : 0];
        static const QRegularExpression pattern("^bytes=(\\d+)-(\\d*)$");
        const auto match = pattern.match(QString::fromLatin1(range));
        const qint64 size = buffer.size();
        const qint64 start = match.hasMatch() ? match.captured(1).toLongLong() : 0;
        const qint64 end = match.hasMatch() && !match.captured(2).isEmpty() ? qMin(match.captured(2).toLongLong(), size - 1)
                                                                            : size - 1;
        if (start >= size || start > end) {
            finish("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + QByteArray::number(size) +
                   "\r\nContent-Length: 0\r\n");
            return;
        }
        QByteArray head = match.hasMatch() ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        head += "Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(end - start + 1) + "\r\n";
        if (match.hasMatch())
            head += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" +
                    QByteArray::number(size) + "\r\n";
        finish(head, method == "HEAD" ? QByteArray() : buffer.mid(start, end - start + 1));
    }
};

const QVector<sdk::Track> tracks{{"one", "Test tone · 220 Hz", "NekoTune", {}, {}, 8000},
                                 {"two", "Test tone · 330 Hz", "NekoTune", {}, {}, 8000}};

class Tones final : public sdk::IMusicProvider {
  public:
    quint16 port = 0;
    void search(const QString &query, const QString &, std::function<void(Result<sdk::SearchPage>)> done) override {
        sdk::SearchPage page;
        for (const auto &track : tracks)
            if (track.title.contains(query, Qt::CaseInsensitive))
                page.tracks.append(track);
        done(page);
    }
    void track(const QString &id, std::function<void(Result<sdk::Track>)> done) override {
        for (const auto &track : tracks)
            if (track.id == id) {
                done(track);
                return;
            }
        done(failure("Track not found"));
    }
    void resolve(const QString &id, sdk::ResolvePurpose, std::function<void(Result<sdk::ResolvedAudio>)> done) override {
        if (id != "one" && id != "two") {
            done(failure("Track not found"));
            return;
        }
        sdk::ResolvedAudio audio;
        audio.url = QUrl(QStringLiteral("http://127.0.0.1:%1/%2.wav").arg(port).arg(id));
        audio.headers.insert("X-NekoTune-Example", "audio-test");
        audio.expiresAt = QDateTime::currentMSecsSinceEpoch() + 60'000;
        audio.extension = "wav";
        done(audio);
    }
};
} // namespace

class TestSourceExample final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::ExtensionContext &context, sdk::Done done) override {
        m_server = std::make_unique<ToneServer>();
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            done(failure("Cannot start the tone server: " + m_server->errorString()));
            return;
        }
        m_tones.port = m_server->serverPort();
        m_registration = context.music().add("tones", &m_tones,
                                                       {.name = "Local test tones / 本地测试音源", .download = true});
        done({});
    }
    void deactivate() override {
        m_registration.reset();
        m_server.reset();
    }

  private:
    Tones m_tones;
    std::unique_ptr<ToneServer> m_server;
    sdk::RegistrationPtr m_registration;
};

#include "plugin.moc"
