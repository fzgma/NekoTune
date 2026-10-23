// Frozen SDK 2.0 ABI fixture. Do not update with the current SDK.
#pragma once
#include <nekotune/result.h>
#include <QHash>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <utility>

namespace nekotune::sdk {
using Params = QVariantMap;
using Reply = std::function<void(Result<Params>)>;

struct Track {
    QString id;
    QString title;
    QString artist;
    QString album;
    QString coverUrl;
    qint64 durationMs = 0;

    Params toParams() const {
        Params result{{"id", id}, {"title", title}};
        if (!artist.isEmpty())
            result.insert("artist", artist);
        if (!album.isEmpty())
            result.insert("album", album);
        if (!coverUrl.isEmpty())
            result.insert("cover_url", coverUrl);
        if (durationMs > 0)
            result.insert("duration_ms", durationMs);
        return result;
    }
};

struct SearchPage {
    QVector<Track> tracks;
    /// Empty when there are no further pages.
    QString cursor;
};

/// url is http(s):// (fetched through the host proxy with headers) or file:// inside dataDirectory().
struct ResolvedAudio {
    QUrl url;
    QHash<QString, QString> headers;
    /// Milliseconds since the Unix epoch; 0 means the URL does not expire.
    qint64 expiresAt = 0;
    QString extension;
    qint64 maxBytes = 0;
    QStringList allowedHosts;
    QStringList contentTypes;

    Params toParams() const {
        QVariantMap headerMap;
        for (auto it = headers.cbegin(); it != headers.cend(); ++it)
            headerMap.insert(it.key(), it.value());
        Params result{{"url", url.toString()}, {"headers", headerMap}};
        if (expiresAt > 0)
            result.insert("expiresAt", expiresAt);
        if (!extension.isEmpty())
            result.insert("extension", extension);
        if (maxBytes > 0)
            result.insert("maxBytes", maxBytes);
        if (!allowedHosts.isEmpty())
            result.insert("allowedHosts", allowedHosts);
        if (!contentTypes.isEmpty())
            result.insert("contentTypes", contentTypes);
        return result;
    }
};

/// Every method is called on the backend thread and must complete its callback exactly once,
/// from any thread. Long work belongs on the plugin's own threads.
class IMusicProvider {
  public:
    virtual ~IMusicProvider() = default;
    virtual void search(const QString &query, const QString &cursor,
                        std::function<void(Result<SearchPage>)> done) = 0;
    virtual void track(const QString &id, std::function<void(Result<Track>)> done) = 0;
    /// purpose is "play" or "download"; downloads may take minutes and return a prepared file://.
    virtual void resolve(const QString &id, const QString &purpose,
                         std::function<void(Result<ResolvedAudio>)> done) = 0;
    /// Called after the host imported a download; the returned map is merged into the task report.
    virtual void downloaded(const QString &id, const QString &path, int songId, bool existing, Reply done) {
        Q_UNUSED(id)
        Q_UNUSED(path)
        Q_UNUSED(songId)
        Q_UNUSED(existing)
        done(Params{});
    }
};

/// Same contract as the JavaScript SDK: search returns {candidates: [...]}, each candidate may carry
/// synced_lyrics/plain_lyrics/krc_lyrics/cover_url/score/song_result. resolve receives one candidate
/// and returns a document, or {candidates} for staged sources.
class ILyricsProvider {
  public:
    virtual ~ILyricsProvider() = default;
    virtual void search(const Params &query, Reply done) = 0;
    virtual void resolve(const Params &candidate, Reply done) = 0;
};
} // namespace nekotune::sdk
