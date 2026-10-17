#include "infrastructure/playback/pcm_decoder.h"
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QtTest>
using namespace nekotune;
class AudioServer final : public QObject {
  public:
    QTcpServer server;
    QByteArray payload;
    int rangeRequests = 0, requests = 0;
    AudioServer() {
        QFile file(QStringLiteral(NEKOTUNE_SOURCE_DIR) + "/tests/fixtures/audio-pipeline/trim.flac");
        if (file.open(QIODevice::ReadOnly))
            payload = file.readAll();
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    ++requests;
                    if (buffer->startsWith("GET /stall")) {
                        buffer->clear();
                        return;
                    }
                    if (buffer->startsWith("GET /expired")) {
                        socket->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    const auto match =
                        QRegularExpression("Range: bytes=(\\d+)-(\\d*)", QRegularExpression::CaseInsensitiveOption)
                            .match(QString::fromLatin1(*buffer));
                    qint64 begin = match.hasMatch() ? match.captured(1).toLongLong() : 0;
                    qint64 end = match.hasMatch() && !match.captured(2).isEmpty() ? match.captured(2).toLongLong()
                                                                                  : payload.size() - 1;
                    end = qMin<qint64>(end, payload.size() - 1);
                    if (begin > end) {
                        socket->write(
                            "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    QByteArray header = match.hasMatch() ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                    if (match.hasMatch()) {
                        ++rangeRequests;
                        header += "Content-Range: bytes " + QByteArray::number(begin) + '-' + QByteArray::number(end) +
                                  '/' + QByteArray::number(payload.size()) + "\r\n";
                    }
                    header += "Content-Type: audio/flac\r\nAccept-Ranges: bytes\r\nContent-Length: " +
                              QByteArray::number(end - begin + 1) + "\r\nConnection: close\r\n\r\n";
                    socket->write(header);
                    socket->write(payload.mid(begin, end - begin + 1));
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url(const QString &path) const {
        return QUrl(QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(path));
    }
};
class AudioNetworkTest final : public QObject {
    Q_OBJECT
  private slots:
    void rangeAndSeek() {
        AudioServer fixture;
        QVERIFY(!fixture.payload.isEmpty());
        QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
        PcmDecoder decoder(fixture.url("audio"), 100);
        QTRY_VERIFY_WITH_TIMEOUT(decoder.info().eof, 5000);
        QVERIFY2(decoder.info().error.isEmpty(), qPrintable(decoder.info().error));
        QVERIFY(fixture.rangeRequests > 0);
        QCOMPARE(decoder.available(), size_t(12000));
    }
    void expiredSourceIsNotAFormatFallback() {
        AudioServer fixture;
        QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
        PcmDecoder decoder(fixture.url("expired"));
        QTRY_VERIFY_WITH_TIMEOUT(decoder.info().eof, 5000);
        QVERIFY(!decoder.info().error.isEmpty());
        QVERIFY(!decoder.info().fallbackEligible);
    }
    void stalledReadCanBeCancelled() {
        AudioServer fixture;
        QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
        auto decoder = std::make_unique<PcmDecoder>(fixture.url("stall"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.requests > 0, 3000);
        QElapsedTimer timer;
        timer.start();
        decoder->cancel();
        decoder.reset();
        QVERIFY2(timer.elapsed() < 1500, qPrintable(QString::number(timer.elapsed())));
    }
};
QTEST_GUILESS_MAIN(AudioNetworkTest)
#include "audio_pipeline_network_test.moc"
