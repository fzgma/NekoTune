#include "infrastructure/extensions/capabilities/audio_proxy.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUuid>

namespace nekotune {
namespace {
const QHash<QString, QString> mediaTypes{
    {"audio/mpeg", "mp3"},     {"audio/mp3", "mp3"},        {"audio/flac", "flac"}, {"audio/x-flac", "flac"},
    {"audio/wav", "wav"},      {"audio/x-wav", "wav"},      {"audio/ogg", "ogg"},   {"application/ogg", "ogg"},
    {"audio/opus", "opus"},    {"audio/mp4", "m4a"},        {"audio/x-m4a", "m4a"}, {"audio/aac", "aac"},
    {"audio/webm", "webm"}};
const QStringList audioExtensions{"mp3", "flac", "wav", "ogg", "opus", "m4a", "aac", "webm"};
constexpr qint64 idleLease = 30LL * 60 * 1000;
constexpr qint64 downloadTimeout = 10LL * 60 * 1000;
constexpr qint64 pumpLimit = 2 * 1024 * 1024;

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
QString randomToken() {
    QByteArray bytes(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), bytes.size() / 4);
    return QString::fromLatin1(bytes.toHex());
}
bool hostAllowed(const QUrl &url, const QStringList &hosts) {
    if (url.scheme() != "https" || !url.userName().isEmpty() || !url.password().isEmpty() ||
        (url.port() != -1 && url.port() != 443))
        return false;
    const auto host = url.host();
    for (const auto &allowed : hosts)
        if (allowed.startsWith("*.") ? host.endsWith(allowed.mid(1)) : host == allowed)
            return true;
    return false;
}
QString baseType(const QByteArray &header) { return QString::fromLatin1(header).section(';', 0, 0).trimmed().toLower(); }
QString origin(const QUrl &url) { return url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString(); }
} // namespace

struct AudioProxy::Stream {
    QString source, id;
    Audio audio;
    qint64 lastAccess = 0;
    QList<QPointer<QTcpSocket>> sockets;
};
struct AudioProxy::Job {
    QString source, task, base, temporary, target, extension;
    Audio audio;
    Completion done;
    Progress progress;
    QPointer<QNetworkReply> reply;
    std::unique_ptr<QFile> file;
    QByteArray prefix;
    bool detected = true, finished = false;
    qint64 received = 0, total = 0, lastProgress = 0;
    std::unique_ptr<QTimer> timeout;
};

Result<AudioProxy::Audio> AudioProxy::validate(const QVariantMap &resolved) {
    Audio audio;
    audio.url = QUrl(resolved.value("url").toString(), QUrl::StrictMode);
    if (!audio.url.isValid() || (audio.url.scheme() != "http" && audio.url.scheme() != "https") || audio.url.host().isEmpty())
        return failure("Audio resolver must return an HTTP(S) URL");
    static const QRegularExpression hop("^(host|connection|content-length|transfer-encoding)$",
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression token("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$");
    const auto headers = resolved.value("headers").toMap();
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (hop.match(it.key()).hasMatch())
            continue;
        const auto value = it.value().toString();
        if (it.value().typeId() != QMetaType::QString || !token.match(it.key()).hasMatch() || value.contains('\r') ||
            value.contains('\n'))
            return failure("Invalid audio header");
        audio.headers.append({it.key().toLatin1(), value.toUtf8()});
    }
    audio.expiresAt = resolved.value("expiresAt").toLongLong();
    audio.extension = resolved.value("extension").toString();
    audio.maxBytes = qMax<qint64>(0, resolved.value("maxBytes").toLongLong());
    audio.restrictHosts = resolved.value("allowedHosts").canConvert<QStringList>() && resolved.contains("allowedHosts");
    audio.allowedHosts = resolved.value("allowedHosts").toStringList();
    audio.restrictTypes = resolved.contains("contentTypes");
    for (const auto &type : resolved.value("contentTypes").toStringList())
        audio.contentTypes.append(type.trimmed().toLower());
    return audio;
}
QString AudioProxy::detectAudio(const QByteArray &prefix) {
    const auto *p = reinterpret_cast<const uchar *>(prefix.constData());
    if (prefix.startsWith("ID3"))
        return "audio/mpeg";
    if (prefix.startsWith("fLaC"))
        return "audio/flac";
    if (prefix.startsWith("OggS"))
        return "audio/ogg";
    if (prefix.startsWith("RIFF") && prefix.mid(8, 4) == "WAVE")
        return "audio/wav";
    if (prefix.size() >= 4 && p[0] == 0xff) {
        if ((p[1] & 0xf6) == 0xf0 && (p[2] & 0x3c) != 0x3c)
            return "audio/aac";
        if ((p[1] & 0xe0) == 0xe0 && (p[1] & 0x18) != 0x08 && (p[1] & 0x06) != 0 && (p[2] & 0xf0) != 0 &&
            (p[2] & 0xf0) != 0xf0 && (p[2] & 0x0c) != 0x0c)
            return "audio/mpeg";
    }
    return {};
}

AudioProxy::AudioProxy(Resolver resolver, QObject *parent) : QObject(parent), m_resolve(std::move(resolver)) {
    connect(&m_server, &QTcpServer::newConnection, this, &AudioProxy::accept);
    m_cleanup.setInterval(60'000);
    connect(&m_cleanup, &QTimer::timeout, this, [this] {
        for (const auto &key : m_streams.keys()) {
            const auto stream = m_streams.value(key);
            stream->sockets.removeIf([](const auto &socket) { return !socket; });
            if (stream->sockets.isEmpty() && now() - stream->lastAccess > idleLease)
                m_streams.remove(key);
        }
    });
}
AudioProxy::~AudioProxy() { close(); }
bool AudioProxy::start() {
    if (m_server.isListening())
        return true;
    m_cleanup.start();
    return m_server.listen(QHostAddress::LocalHost, 0);
}
void AudioProxy::close() {
    m_cleanup.stop();
    for (const auto &task : m_downloads.keys())
        cancelDownload(task);
    for (const auto &key : m_streams.keys())
        release(QStringLiteral("http://127.0.0.1/") + key);
    m_server.close();
}

void AudioProxy::lease(const QString &source, const QString &id, const QVariantMap &resolved, Completion done) {
    const auto audio = validate(resolved);
    if (!audio) {
        done(audio.error());
        return;
    }
    if (!start()) {
        done(failure("Audio proxy is unavailable: " + m_server.errorString()));
        return;
    }
    const auto token = randomToken();
    auto stream = std::make_shared<Stream>();
    stream->source = source;
    stream->id = id;
    stream->audio = audio.value();
    stream->lastAccess = now();
    m_streams.insert(token, stream);
    auto extension = audio.value().extension;
    if (extension.isEmpty())
        extension = QFileInfo(audio.value().url.path()).suffix();
    QVariantMap reply{{"url", QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server.serverPort()).arg(token)},
                      {"extension", extension.isEmpty() ? QStringLiteral("mp3") : extension}};
    if (audio.value().maxBytes > 0)
        reply.insert("maxBytes", audio.value().maxBytes);
    if (audio.value().restrictTypes)
        reply.insert("contentTypes", audio.value().contentTypes);
    done(reply);
}
void AudioProxy::release(const QString &url) {
    const auto stream = m_streams.take(QUrl(url).path().mid(1));
    if (!stream)
        return;
    for (const auto &socket : std::as_const(stream->sockets))
        if (socket)
            socket->abort();
}
void AudioProxy::revoke(const QString &extensionId) {
    const auto prefix = extensionId + "/";
    for (const auto &job : m_downloads)
        if (job->source.startsWith(prefix))
            cancelDownload(job->task);
    for (const auto &key : m_streams.keys())
        if (m_streams.value(key)->source.startsWith(prefix))
            release(QStringLiteral("http://127.0.0.1/") + key);
}

void AudioProxy::upstream(const Audio &audio, const QByteArray &method, const QByteArray &range, QObject *owner,
                          int redirects, Ready ready) {
    if (redirects > 5) {
        ready(nullptr, "Too many audio redirects");
        return;
    }
    if (audio.restrictHosts && !hostAllowed(audio.url, audio.allowedHosts)) {
        ready(nullptr, "Audio host is not allowed");
        return;
    }
    QNetworkRequest request(audio.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30'000);
    for (const auto &[name, value] : audio.headers)
        request.setRawHeader(name, value);
    if (!range.isEmpty())
        request.setRawHeader("Range", range);
    auto *reply = m_network.sendCustomRequest(request, method);
    reply->setParent(owner);
    reply->setReadBufferSize(pumpLimit);
    auto delivered = std::make_shared<bool>(false);
    auto deliver = [this, reply, audio, method, range, owner, redirects, ready, delivered] {
        if (*delivered)
            return;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (!status.isValid()) {
            if (reply->isFinished()) {
                *delivered = true;
                const auto error = reply->errorString();
                reply->deleteLater();
                ready(nullptr, error);
            }
            return;
        }
        *delivered = true;
        const int code = status.toInt();
        const auto location = reply->header(QNetworkRequest::LocationHeader).toUrl();
        if (QList<int>{301, 302, 303, 307, 308}.contains(code) && location.isValid()) {
            reply->abort();
            reply->deleteLater();
            auto next = audio;
            next.url = audio.url.resolved(location);
            if (next.url.scheme() != "http" && next.url.scheme() != "https") {
                ready(nullptr, "Invalid audio redirect");
                return;
            }
            if (origin(next.url) != origin(audio.url))
                next.headers.removeIf([](const auto &header) {
                    const auto name = header.first.toLower();
                    return name == "authorization" || name == "cookie";
                });
            upstream(next, method, range, owner, redirects + 1, ready);
            return;
        }
        ready(reply, {});
    };
    connect(reply, &QNetworkReply::metaDataChanged, this, deliver);
    connect(reply, &QNetworkReply::finished, this, deliver);
}

void AudioProxy::accept() {
    while (auto *socket = m_server.nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        auto *deadline = new QTimer(socket);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, socket, &QTcpSocket::abort);
        deadline->start(10'000);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, deadline] {
            if (!deadline->isActive())
                return;
            *buffer += socket->readAll();
            const auto end = buffer->indexOf("\r\n\r\n");
            if (end < 0) {
                if (buffer->size() > 16 * 1024)
                    socket->abort();
                return;
            }
            deadline->stop();
            const auto lines = buffer->left(end).split('\n');
            const auto requestLine = lines.value(0).trimmed().split(' ');
            QByteArray range;
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const auto line = lines[i].trimmed();
                const auto colon = line.indexOf(':');
                if (colon > 0 && line.left(colon).trimmed().toLower() == "range")
                    range = line.mid(colon + 1).trimmed();
            }
            serve(socket, requestLine.value(0), QString::fromLatin1(requestLine.value(1)).section('?', 0, 0).mid(1),
                  range);
        });
    }
}
void AudioProxy::serve(QTcpSocket *socket, const QByteArray &method, const QString &token, const QByteArray &range) {
    const auto stream = m_streams.value(token);
    if (!stream || (method != "GET" && method != "HEAD")) {
        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    stream->lastAccess = now();
    stream->sockets.append(socket);
    if (stream->audio.expiresAt && stream->audio.expiresAt <= now() + 1000) {
        QPointer<AudioProxy> guard(this);
        QPointer<QTcpSocket> client(socket);
        m_resolve(stream->source, stream->id, "play",
                  [guard, client, stream, method, range, token](Result<QVariantMap> result) {
                      if (!guard || !client)
                          return;
                      const auto audio = result ? validate(result.value()) : Result<Audio>(result.error());
                      if (!audio || guard->m_streams.value(token) != stream) {
                          client->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                          client->disconnectFromHost();
                          return;
                      }
                      stream->audio = audio.value();
                      guard->respond(client, stream, method, range, false);
                  });
        return;
    }
    respond(socket, stream, method, range, false);
}
void AudioProxy::respond(QTcpSocket *socket, const std::shared_ptr<Stream> &stream, const QByteArray &method,
                         const QByteArray &range, bool retried) {
    QPointer<QTcpSocket> client(socket);
    QPointer<AudioProxy> guard(this);
    upstream(stream->audio, method, range, socket, 0, [guard, client, stream, method, range, retried](QNetworkReply *reply, const QString &) {
        if (!guard || !client) {
            if (reply)
                reply->deleteLater();
            return;
        }
        const auto fail = [client] {
            client->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 24\r\nConnection: close\r\n\r\n"
                          "Audio source unavailable");
            client->disconnectFromHost();
        };
        if (!reply) {
            fail();
            return;
        }
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!retried && (code == 401 || code == 403 || code == 410)) {
            reply->abort();
            reply->deleteLater();
            guard->m_resolve(stream->source, stream->id, "play", [guard, client, stream, method, range, fail](Result<QVariantMap> result) {
                if (!guard || !client)
                    return;
                const auto audio = result ? validate(result.value()) : Result<Audio>(result.error());
                if (!audio) {
                    fail();
                    return;
                }
                stream->audio = audio.value();
                guard->respond(client, stream, method, range, true);
            });
            return;
        }
        QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + " " +
                          reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toByteArray() + "\r\n";
        for (const auto *name : {"Content-Type", "Content-Length", "Content-Range", "Accept-Ranges", "ETag", "Last-Modified"})
            if (reply->hasRawHeader(name))
                head += QByteArray(name) + ": " + reply->rawHeader(name) + "\r\n";
        head += "Connection: close\r\n\r\n";
        client->write(head);
        const auto pump = [client, reply] {
            if (!client || !reply)
                return;
            while (reply->bytesAvailable() > 0 && client->bytesToWrite() < pumpLimit)
                client->write(reply->read(64 * 1024));
            if (reply->isFinished() && reply->bytesAvailable() == 0 && client->bytesToWrite() == 0) {
                client->disconnectFromHost();
            }
        };
        QObject::connect(reply, &QNetworkReply::readyRead, client, pump);
        QObject::connect(reply, &QNetworkReply::finished, client, [client, reply, pump] {
            if (reply->error() != QNetworkReply::NoError && reply->error() != QNetworkReply::OperationCanceledError &&
                client) {
                client->abort();
                return;
            }
            pump();
        });
        QObject::connect(client, &QTcpSocket::bytesWritten, reply, pump);
        QObject::connect(client, &QTcpSocket::disconnected, reply, &QNetworkReply::abort);
        pump();
    });
}

void AudioProxy::download(const QString &source, const QString &id, const QString &base, const QString &task,
                          const QString &dataDirectory, Progress progress, Completion done) {
    auto job = std::make_shared<Job>();
    job->source = source;
    job->task = task.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : task;
    job->base = base;
    job->done = std::move(done);
    job->progress = std::move(progress);
    job->timeout = std::make_unique<QTimer>();
    job->timeout->setSingleShot(true);
    connect(job->timeout.get(), &QTimer::timeout, this, [this, job] { finishDownload(job, failure("Download timed out")); });
    job->timeout->start(downloadTimeout);
    m_downloads.insert(job->task, job);
    QPointer<AudioProxy> guard(this);
    m_resolve(source, id, "download", [guard, job, dataDirectory](Result<QVariantMap> result) {
        if (!guard || job->finished)
            return;
        if (!result) {
            guard->finishDownload(job, result.error());
            return;
        }
        const QUrl url(result.value().value("url").toString());
        if (url.isLocalFile()) {
            // Prepared files must come from the extension's own data directory.
            const auto file = QFileInfo(url.toLocalFile()).canonicalFilePath();
            const auto root = QFileInfo(dataDirectory).canonicalFilePath();
            if (file.isEmpty() || root.isEmpty() || !file.startsWith(root + "/") || !QFileInfo(file).isFile()) {
                guard->finishDownload(job, failure("Extension audio files must be inside the extension data directory"));
                return;
            }
            QFile input(file);
            if (!input.open(QIODevice::ReadOnly)) {
                guard->finishDownload(job, failure(input.errorString(), ErrorCode::Io));
                return;
            }
            const auto detected = mediaTypes.value(detectAudio(input.peek(64)));
            auto extension = result.value().value("extension").toString().toLower();
            if (!audioExtensions.contains(extension))
                extension = QFileInfo(file).suffix().toLower();
            if (!detected.isEmpty())
                extension = detected;
            if (!audioExtensions.contains(extension)) {
                guard->finishDownload(job, failure("Response is not a supported audio type"));
                return;
            }
            const auto target = job->base + "." + extension;
            if (!QFileInfo::exists(target) && !QFile::copy(file, target)) {
                guard->finishDownload(job, failure("Cannot copy downloaded audio", ErrorCode::Io));
                return;
            }
            guard->finishDownload(job, QVariantMap{{"path", target}});
            return;
        }
        const auto audio = validate(result.value());
        if (!audio) {
            guard->finishDownload(job, audio.error());
            return;
        }
        job->audio = audio.value();
        guard->upstream(job->audio, "GET", {}, guard, 0, [guard, job](QNetworkReply *reply, const QString &error) {
            if (!guard || job->finished) {
                if (reply)
                    reply->deleteLater();
                return;
            }
            if (!reply) {
                guard->finishDownload(job, failure(error));
                return;
            }
            job->reply = reply;
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code < 200 || code >= 300) {
                guard->finishDownload(job, failure(QStringLiteral("Download failed: HTTP %1").arg(code)));
                return;
            }
            const auto contentType = baseType(reply->rawHeader("Content-Type"));
            const auto &allowed = job->audio.contentTypes;
            const bool sniff = job->audio.restrictTypes &&
                               QStringList{"", "application/octet-stream", "binary/octet-stream"}.contains(contentType);
            const auto supported = [allowed](const QString &type) {
                for (const auto &candidate : allowed)
                    if (candidate == type || (mediaTypes.contains(candidate) && mediaTypes.value(candidate) == mediaTypes.value(type)))
                        return true;
                return false;
            };
            const auto unsupported = QStringLiteral("Response is not a supported audio type (%1)")
                                         .arg(contentType.isEmpty() ? QStringLiteral("missing Content-Type") : contentType);
            if (job->audio.restrictTypes && !sniff && !supported(contentType)) {
                guard->finishDownload(job, failure(unsupported));
                return;
            }
            job->extension = mediaTypes.value(contentType);
            if (job->extension.isEmpty())
                job->extension = audioExtensions.contains(job->audio.extension.toLower()) ? job->audio.extension.toLower()
                                                                                          : QStringLiteral("mp3");
            job->detected = !sniff;
            job->total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            job->temporary = job->base + ".partial-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            job->file = std::make_unique<QFile>(job->temporary);
            if (!job->file->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                guard->finishDownload(job, failure(job->file->errorString(), ErrorCode::Io));
                return;
            }
            const auto read = [guard, job, supported, unsupported] {
                if (!guard || job->finished || !job->reply)
                    return;
                const auto chunk = job->reply->readAll();
                job->received += chunk.size();
                if (job->audio.maxBytes && job->received > job->audio.maxBytes) {
                    guard->finishDownload(job, failure("Audio exceeds source download limit"));
                    return;
                }
                if (!job->detected) {
                    job->prefix += chunk.left(64 - job->prefix.size());
                    const auto type = detectAudio(job->prefix);
                    if (!type.isEmpty() && supported(type)) {
                        job->extension = mediaTypes.value(type);
                        job->detected = true;
                    } else if (job->prefix.size() >= 64) {
                        guard->finishDownload(job, failure(unsupported));
                        return;
                    }
                }
                if (job->file->write(chunk) != chunk.size()) {
                    guard->finishDownload(job, failure(job->file->errorString(), ErrorCode::Io));
                    return;
                }
                if (job->progress && now() - job->lastProgress > 100) {
                    job->lastProgress = now();
                    job->progress(job->received, job->total);
                }
            };
            connect(reply, &QNetworkReply::readyRead, guard, read);
            const auto complete = [guard, job, read, unsupported] {
                if (!guard || job->finished || !job->reply)
                    return;
                read();
                if (job->finished)
                    return;
                if (job->reply->error() != QNetworkReply::NoError) {
                    guard->finishDownload(job, failure("Download failed: " + job->reply->errorString()));
                    return;
                }
                if (!job->received) {
                    guard->finishDownload(job, failure("Downloaded audio is empty"));
                    return;
                }
                if (!job->detected) {
                    guard->finishDownload(job, failure(unsupported));
                    return;
                }
                job->file->close();
                const auto target = job->base + "." + job->extension;
                if (!QFileInfo::exists(target) && !QFile::rename(job->temporary, target)) {
                    guard->finishDownload(job, failure("Cannot save downloaded audio", ErrorCode::Io));
                    return;
                }
                guard->finishDownload(job, QVariantMap{{"path", target}});
            };
            connect(reply, &QNetworkReply::finished, guard, complete);
            if (reply->isFinished())
                complete();
            else
                read();
        });
    });
}
void AudioProxy::cancelDownload(const QString &task) {
    if (const auto job = m_downloads.value(task))
        finishDownload(job, failure("Download cancelled", ErrorCode::Cancelled));
}
void AudioProxy::finishDownload(const std::shared_ptr<Job> &job, Result<QVariantMap> result) {
    if (job->finished)
        return;
    job->finished = true;
    job->timeout.reset();
    m_downloads.remove(job->task);
    if (job->reply) {
        job->reply->disconnect(this);
        job->reply->abort();
        job->reply->deleteLater();
    }
    if (job->file) {
        job->file->close();
        job->file.reset();
    }
    if (!job->temporary.isEmpty())
        QFile::remove(job->temporary);
    if (const auto done = std::exchange(job->done, {}))
        done(std::move(result));
}
} // namespace nekotune
