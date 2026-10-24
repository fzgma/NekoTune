#pragma once
#include "domain/extensions/extension_backend.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <memory>

class QNetworkReply;
class QTcpSocket;

namespace nekotune {
/// Loopback HTTP proxy for extension audio. Qt Multimedia cannot attach request headers, so the
/// player receives http://127.0.0.1:<port>/<token>; the proxy adds the extension's headers,
/// enforces allowedHosts, follows redirects and re-resolves expired or rejected URLs.
/// Downloads use the same upstream rules and write straight to the reserved library path.
/// Lives on the backend thread.
class AudioProxy final : public QObject {
    Q_OBJECT
  public:
    using Completion = IExtensionBackend::Completion;
    /// Resolve (source, id, purpose) to a ResolvedAudio map, as returned by the provider.
    using Resolver = std::function<void(const QString &, const QString &, const QString &, Completion)>;
    using Progress = std::function<void(qint64 received, qint64 total)>;
    struct Audio {
        QUrl url;
        QList<QPair<QByteArray, QByteArray>> headers;
        qint64 expiresAt = 0;
        QString extension;
        qint64 maxBytes = 0;
        bool restrictHosts = false;
        QStringList allowedHosts;
        bool restrictTypes = false;
        QStringList contentTypes;
    };
    /// Parse and sanitize a provider result; rejects non-HTTP(S) URLs and header injection.
    static Result<Audio> validate(const QVariantMap &resolved);
    /// MIME type sniffed from the first bytes of an audio file; empty when unknown.
    static QString detectAudio(const QByteArray &prefix);

    explicit AudioProxy(Resolver resolver, QObject *parent = nullptr);
    ~AudioProxy() override;
    bool start();
    void close();
    /// Lease a proxy URL for an already resolved provider result:
    /// {url, extension, maxBytes?, contentTypes?}.
    void lease(const QString &source, const QString &id, const QVariantMap &resolved, Completion done);
    void release(const QString &url);
    /// Drop every lease and download of one extension.
    void revoke(const QString &extensionId);
    /// Download to "<base>.<ext>" and reply {path}. dataDirectory bounds file:// results.
    void download(const QString &source, const QString &id, const QString &base, const QString &task,
                  const QString &dataDirectory, Progress progress, Completion done);
    void cancelDownload(const QString &task);

  private:
    struct Stream;
    struct Job;
    using Ready = std::function<void(QNetworkReply *, const QString &error)>;
    void upstream(const Audio &audio, const QByteArray &method, const QByteArray &range, QObject *owner, int redirects,
                  Ready ready);
    void accept();
    void serve(QTcpSocket *socket, const QByteArray &method, const QString &token, const QByteArray &range);
    void respond(QTcpSocket *socket, const std::shared_ptr<Stream> &stream, const QByteArray &method,
                 const QByteArray &range, bool retried);
    void finishDownload(const std::shared_ptr<Job> &job, Result<QVariantMap> result);

    Resolver m_resolve;
    QTcpServer m_server;
    QNetworkAccessManager m_network;
    QHash<QString, std::shared_ptr<Stream>> m_streams;
    QHash<QString, std::shared_ptr<Job>> m_downloads;
    QTimer m_cleanup;
};
} // namespace nekotune
