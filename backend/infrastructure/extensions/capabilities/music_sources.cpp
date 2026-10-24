#include "infrastructure/extensions/capabilities/music_sources.h"
#include "infrastructure/extensions/capabilities/capability_host.h"
#include "infrastructure/extensions/runtime/call_guard.h"
#include <QFileInfo>

namespace nekotune {
MusicSources::MusicSources(CapabilityHost &host, QObject *parent)
    : IMusicSourceCatalog(parent), m_host(host),
      m_audio([this](const QString &source, const QString &id, const QString &purpose,
                     AudioProxy::Completion done) {
          resolve(source, id, purpose, [done](Result<sdk::ResolvedAudio> audio) {
              done(audio ? Result<QVariantMap>(audio.value().toParams())
                         : Result<QVariantMap>(audio.error()));
          });
      }) {}

void MusicSources::add(const QString &extensionId, const QString &localId, sdk::IMusicProvider *provider,
                       QVariantMap descriptor) {
    m_sources[extensionId].insert(localId, {provider, std::move(descriptor)});
}
void MusicSources::remove(const QString &extensionId, const QString &localId) {
    const auto found = m_sources.find(extensionId);
    if (found != m_sources.end())
        found->remove(localId);
}
void MusicSources::removeExtension(const QString &extensionId) {
    m_audio.revoke(extensionId);
    m_sources.remove(extensionId);
}
void MusicSources::close() { m_audio.close(); }
QVariantList MusicSources::sources() const {
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
Result<sdk::IMusicProvider *> MusicSources::provider(const QString &source) const {
    const auto slash = source.indexOf('/');
    const auto owner = source.left(slash), local = source.mid(slash + 1);
    const auto found = m_sources.value(owner).value(local).provider;
    if (slash <= 0 || !m_host.running(owner) || !found)
        return failure("Source is unavailable");
    return found;
}
void MusicSources::search(const QString &source, const QString &query, const QString &cursor,
                          std::function<void(Result<sdk::SearchPage>)> done) {
    const auto found = provider(source);
    if (!found) {
        done(found.error());
        return;
    }
    const auto owner = source.section('/', 0, 0);
    const auto reply =
        m_host.pendingCalls(owner)->add<sdk::SearchPage>(m_host.owner(), std::move(done), timeouts::call);
    m_host.guarded(owner, [&] { found.value()->search(query, cursor, reply); });
}
void MusicSources::track(const QString &source, const QString &id,
                         std::function<void(Result<sdk::Track>)> done) {
    const auto found = provider(source);
    if (!found) {
        done(found.error());
        return;
    }
    const auto owner = source.section('/', 0, 0);
    const auto reply =
        m_host.pendingCalls(owner)->add<sdk::Track>(m_host.owner(), std::move(done), timeouts::call);
    m_host.guarded(owner, [&] { found.value()->track(id, reply); });
}
void MusicSources::resolve(const QString &source, const QString &id, const QString &purpose,
                           std::function<void(Result<sdk::ResolvedAudio>)> done) {
    const auto found = provider(source);
    if (!found) {
        done(found.error());
        return;
    }
    const auto owner = source.section('/', 0, 0);
    const auto reply = m_host.pendingCalls(owner)->add<sdk::ResolvedAudio>(
        m_host.owner(), std::move(done), purpose == "download" ? timeouts::download : timeouts::call);
    m_host.guarded(owner, [&] {
        found.value()->resolve(id, purpose == "download" ? sdk::ResolvePurpose::Download : sdk::ResolvePurpose::Play,
                               reply);
    });
}
void MusicSources::resolvePlayback(const QString &source, const QString &id,
                                   std::function<void(Result<Playback>)> done) {
    const auto data = QFileInfo(m_host.dataDirectory(source.section('/', 0, 0))).canonicalFilePath();
    QPointer<MusicSources> guard(this);
    resolve(source, id, "play", [guard, source, id, data, done](Result<sdk::ResolvedAudio> result) {
        if (!guard)
            return;
        if (!result) {
            done(result.error());
            return;
        }
        const auto url = result.value().url;
        if (!url.isLocalFile()) {
            guard->m_audio.lease(source, id, result.value().toParams(), [done](Result<QVariantMap> lease) {
                if (!lease)
                    done(lease.error());
                else
                    done(Playback{QUrl(lease.value().value("url").toString()),
                                  lease.value().value("extension").toString()});
            });
            return;
        }
        // Prepared local files play directly but must stay inside the extension's data.
        const auto file = QFileInfo(url.toLocalFile()).canonicalFilePath();
        if (data.isEmpty() || file.isEmpty() || !file.startsWith(data + "/") || !QFileInfo(file).isFile()) {
            done(failure("Extension audio files must be inside the extension data directory"));
            return;
        }
        done(Playback{QUrl::fromLocalFile(file), {}});
    });
}
void MusicSources::release(const QUrl &url) { m_audio.release(url.toString()); }
void MusicSources::download(const QString &source, const QString &id, const QString &base,
                            const QString &task, Progress progress,
                            std::function<void(Result<QString>)> done) {
    m_audio.download(source, id, base, task, m_host.dataDirectory(source.section('/', 0, 0)),
                     std::move(progress), [done](Result<QVariantMap> result) {
                         if (!result)
                             done(result.error());
                         else
                             done(result.value().value("path").toString());
                     });
}
void MusicSources::cancelDownload(const QString &task) { m_audio.cancelDownload(task); }
void MusicSources::downloaded(const QString &source, const QString &id, const QString &path, int songId,
                              bool existing, std::function<void(Result<QVariantMap>)> done) {
    const auto found = provider(source);
    if (!found) {
        done(found.error());
        return;
    }
    const auto owner = source.section('/', 0, 0);
    const auto reply = m_host.pendingCalls(owner)->add(m_host.owner(), std::move(done), timeouts::downloaded);
    m_host.guarded(owner, [&] { found.value()->downloaded(id, path, songId, existing, reply); });
}
} // namespace nekotune
