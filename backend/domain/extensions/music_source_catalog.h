#pragma once
#include "domain/result.h"
#include <nekotune/sdk/providers.h>
#include <QObject>
#include <QUrl>

namespace nekotune {
/// Music sources registered by running extensions. Lives on the backend thread; callbacks
/// complete once on that thread.
class IMusicSourceCatalog : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    using Progress = std::function<void(qint64 received, qint64 total)>;
    struct Playback {
        QUrl url;
        /// File extension suggested by the source; empty when unknown.
        QString extension;
    };
    /// Public descriptors ({id, localId, extensionId, name, ...}) in installation order.
    virtual QVariantList sources() const = 0;
    virtual void search(const QString &source, const QString &query, const QString &cursor,
                        std::function<void(Result<sdk::SearchPage>)> done) = 0;
    virtual void track(const QString &source, const QString &id, std::function<void(Result<sdk::Track>)> done) = 0;
    /// A loopback proxy lease for HTTP(S) results, or a prepared file inside the extension's data.
    virtual void resolvePlayback(const QString &source, const QString &id,
                                 std::function<void(Result<Playback>)> done) = 0;
    virtual void release(const QUrl &url) = 0;
    /// Download to "<base>.<ext>"; completes with the written path.
    virtual void download(const QString &source, const QString &id, const QString &base, const QString &task,
                          Progress progress, std::function<void(Result<QString>)> done) = 0;
    virtual void cancelDownload(const QString &task) = 0;
    /// Called after the host imported a download; the map is merged into the task report.
    virtual void downloaded(const QString &source, const QString &id, const QString &path, int songId,
                            bool existing, std::function<void(Result<QVariantMap>)> done) = 0;
  signals:
    void sourcesChanged();
};
} // namespace nekotune
