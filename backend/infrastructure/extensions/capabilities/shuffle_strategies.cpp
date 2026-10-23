#include "infrastructure/extensions/capabilities/shuffle_strategies.h"
#include "infrastructure/extensions/capabilities/capability_host.h"
#include "infrastructure/extensions/runtime/call_guard.h"
#include <algorithm>

namespace nekotune {
ShuffleStrategies::ShuffleStrategies(CapabilityHost &host, QObject *parent) : QObject(parent), m_host(host) {}
void ShuffleStrategies::add(const QString &extensionId, const QString &localId,
                            sdk::IShuffleProvider *provider, QString name) {
    m_strategies[extensionId].insert(localId, {provider, std::move(name), ++m_generation, false});
    emit catalogueChanged();
}
void ShuffleStrategies::remove(const QString &extensionId, const QString &localId) {
    const auto found = m_strategies.find(extensionId);
    if (found != m_strategies.end())
        found->remove(localId);
    emit catalogueChanged();
}
void ShuffleStrategies::removeExtension(const QString &extensionId) {
    m_strategies.remove(extensionId);
    emit catalogueChanged();
}
QList<ShuffleStrategyDescriptor> ShuffleStrategies::strategies() const {
    QList<ShuffleStrategyDescriptor> result;
    for (const auto &owner : m_host.extensionOrder()) {
        if (!m_host.running(owner))
            continue;
        const auto strategies = m_strategies.value(owner);
        for (auto it = strategies.cbegin(); it != strategies.cend(); ++it) {
            if (!it->failed)
                result.append({owner + '/' + it.key(), it->name, it->generation});
        }
    }
    return result;
}

Result<sdk::ShuffleSelection> ShuffleStrategies::select(const QString &id, quint64 generation,
                                                        const sdk::ShuffleRequest &request) {
    if (request.queue.isEmpty())
        return failure("Queue is empty");
    const auto owner = id.section('/', 0, 0);
    const auto local = id.section('/', 1);
    if (!m_host.running(owner))
        return failure("Shuffle extension unavailable: " + owner, ErrorCode::Unavailable);
    const auto strategies = m_strategies.value(owner);
    const auto found = strategies.constFind(local);
    if (found == strategies.cend() || found->generation != generation || found->failed)
        return failure("Shuffle registration unavailable: " + id, ErrorCode::Unavailable);
    auto *provider = found->provider;
    Result<sdk::ShuffleSelection> result = failure("Shuffle selection failed");
    if (const auto error = runAttributed(owner, [&] { result = provider->select(request); }))
        result = failure(QString(*error).replace("Unhandled exception", "Unhandled shuffle exception"));
    const auto currentStrategies = m_strategies.value(owner);
    const auto live = currentStrategies.constFind(local);
    if (!m_host.running(owner) || live == currentStrategies.cend() || live->generation != generation)
        return failure("Shuffle registration changed during selection", ErrorCode::Unavailable);
    if (result) {
        const int selected = result.value().queueId;
        const bool exists =
            std::any_of(request.queue.cbegin(), request.queue.cend(),
                        [selected](const auto &track) { return track.queueId == selected && selected > 0; });
        if (!exists)
            result = failure("Shuffle strategy selected an unavailable queue item");
    }
    if (!result) {
        // Re-find after foreign code; registration disposal may have invalidated the iterator.
        auto &currentStrategies = m_strategies[owner];
        auto current = currentStrategies.find(local);
        if (current != currentStrategies.end() && current->generation == generation)
            current->failed = true;
        m_host.recordLog(owner, "error", "Shuffle strategy " + id + ": " + result.error().message);
        emit catalogueChanged();
    }
    return result;
}

} // namespace nekotune
