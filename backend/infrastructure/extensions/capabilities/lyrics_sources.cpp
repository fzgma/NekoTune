#include "infrastructure/extensions/capabilities/lyrics_sources.h"
#include "infrastructure/extensions/capabilities/capability_host.h"
#include "infrastructure/extensions/runtime/call_guard.h"

namespace nekotune {
namespace {
sdk::LyricsQuery queryFromParams(const QVariantMap &params) {
    return {params.value("title").toString(), params.value("artist").toString(),
            params.value("album").toString(), params.value("track_id").toString(),
            params.value("duration_ms").toLongLong()};
}
QVariantMap documentToParams(const sdk::LyricsDocument &document) {
    QVariantMap params;
    for (const auto &[key, value] : {std::pair{"synced_lyrics", document.syncedLyrics},
                                     {"plain_lyrics", document.plainLyrics},
                                     {"krc_lyrics", document.krcLyrics},
                                     {"cover_url", document.coverUrl}})
        if (!value.isEmpty())
            params.insert(key, value);
    if (document.instrumental)
        params.insert("instrumental", true);
    return params;
}
QVariantMap candidateToParams(const sdk::LyricsCandidate &candidate) {
    auto params = candidate.extra;
    for (const auto &[key, value] :
         {std::pair{"title", candidate.title}, {"artist", candidate.artist}, {"album", candidate.album}})
        if (!value.isEmpty())
            params.insert(key, value);
    if (candidate.durationMs > 0)
        params.insert("duration_ms", candidate.durationMs);
    params.insert("score", candidate.score);
    if (candidate.songResult)
        params.insert("song_result", true);
    params.insert(documentToParams(candidate.document));
    return params;
}
sdk::LyricsCandidate candidateFromParams(const QVariantMap &params) {
    sdk::LyricsCandidate candidate;
    candidate.title = params.value("title").toString();
    candidate.artist = params.value("artist").toString();
    candidate.album = params.value("album").toString();
    candidate.durationMs = params.value("duration_ms").toLongLong();
    candidate.score = params.value("score", 1.0).toDouble();
    candidate.songResult = params.value("song_result").toBool();
    candidate.document = {params.value("synced_lyrics").toString(), params.value("plain_lyrics").toString(),
                          params.value("krc_lyrics").toString(), params.value("cover_url").toString(),
                          params.value("instrumental").toBool()};
    candidate.extra = params;
    for (const auto *key : {"title", "artist", "album", "duration_ms", "score", "song_result",
                            "synced_lyrics", "plain_lyrics", "krc_lyrics", "cover_url", "instrumental"})
        candidate.extra.remove(key);
    return candidate;
}
QVariantMap candidatesToParams(const QVector<sdk::LyricsCandidate> &candidates) {
    QVariantList list;
    for (const auto &candidate : candidates)
        list.append(candidateToParams(candidate));
    return {{"candidates", list}};
}
} // namespace

LyricsSources::LyricsSources(CapabilityHost &host, QObject *parent)
    : ILyricsSourceCatalog(parent), m_host(host) {}
void LyricsSources::add(const QString &extensionId, const QString &localId, sdk::ILyricsProvider *provider,
                        QVariantMap descriptor) {
    m_sources[extensionId].insert(localId, {provider, std::move(descriptor)});
}
void LyricsSources::remove(const QString &extensionId, const QString &localId) {
    const auto found = m_sources.find(extensionId);
    if (found != m_sources.end())
        found->remove(localId);
}
void LyricsSources::removeExtension(const QString &extensionId) { m_sources.remove(extensionId); }
QVariantList LyricsSources::sources() const {
    QVariantList result;
    for (const auto &owner : m_host.extensionOrder()) {
        if (!m_host.running(owner))
            continue;
        const auto sources = m_sources.value(owner);
        for (const auto &source : sources)
            result.append(source.descriptor);
    }
    return result;
}
void LyricsSources::search(const QString &source, const QVariantMap &query, Reply done) {
    call(source, query, true, std::move(done));
}
void LyricsSources::resolve(const QString &source, const QVariantMap &candidate, Reply done) {
    call(source, candidate, false, std::move(done));
}
void LyricsSources::call(const QString &source, const QVariantMap &params, bool search, Reply done) {
    const auto slash = source.indexOf('/');
    const auto owner = source.left(slash), local = source.mid(slash + 1);
    auto *provider = m_sources.value(owner).value(local).provider;
    if (slash <= 0 || !m_host.running(owner) || !provider) {
        done(failure("Source is unavailable"));
        return;
    }
    const auto reply = m_host.pendingCalls(owner)->add(m_host.owner(), std::move(done), timeouts::call);
    m_host.guarded(owner, [&] {
        if (search) {
            provider->search(queryFromParams(params), [reply](Result<QVector<sdk::LyricsCandidate>> result) {
                reply(result ? Result<QVariantMap>(candidatesToParams(result.value()))
                             : Result<QVariantMap>(result.error()));
            });
        } else {
            provider->resolve(candidateFromParams(params), [reply](Result<sdk::LyricsResolution> result) {
                if (!result)
                    reply(result.error());
                else if (result.value().document)
                    reply(documentToParams(*result.value().document));
                else
                    reply(candidatesToParams(result.value().candidates));
            });
        }
    });
}
} // namespace nekotune
