#include "infrastructure/extensions/capabilities/lyrics_sources.h"
#include "infrastructure/extensions/capabilities/capability_host.h"
#include "infrastructure/extensions/runtime/call_guard.h"

namespace nekotune {
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
        if (search)
            provider->search(params, reply);
        else
            provider->resolve(params, reply);
    });
}
} // namespace nekotune
