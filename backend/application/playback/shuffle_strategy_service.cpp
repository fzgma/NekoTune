#include "application/playback/shuffle_strategy_service.h"
#include <QFileInfo>

namespace nekotune {
namespace {
class ExtensionShuffleStrategy final : public IShuffleStrategy {
  public:
    ExtensionShuffleStrategy(QString id, quint64 generation, ShuffleStrategyService::Selector selector)
        : m_id(std::move(id)), m_generation(generation), m_select(std::move(selector)) {}
    QString id() const override { return m_id; }
    bool allowsImmediateRepeat() const override { return true; }
    std::unique_ptr<IShuffleStrategy> clone() const override {
        return std::make_unique<ExtensionShuffleStrategy>(*this);
    }
    void reset(const QList<int> &ids, int current) override {
        m_state.clear();
        m_fallback.reset(ids, current);
    }
    void syncQueue(const QList<int> &ids, int current) override { m_fallback.syncQueue(ids, current); }
    void reset(const PlayerQueue &queue) override {
        IShuffleStrategy::reset(queue);
        updateQueue(queue);
    }
    void syncQueue(const PlayerQueue &queue) override {
        IShuffleStrategy::syncQueue(queue);
        updateQueue(queue);
    }
    int proposeNext(int current) override {
        if (m_tracks.isEmpty()) return 0;
        const sdk::ShuffleRequest request{m_tracks, current, static_cast<quint32>(m_random()), m_state};
        const auto result = m_select(m_id, m_generation, request);
        if (result) {
            for (const auto &track : m_tracks) {
                if (track.queueId == result.value().queueId) {
                    m_state = result.value().state;
                    return track.queueId;
                }
            }
        }
        return m_fallback.proposeNext(current);
    }
    void confirmSelection(int id) override { m_fallback.confirmSelection(id); }
  private:
    void updateQueue(const PlayerQueue &queue) {
        m_tracks.clear();
        for (int i = 0; i < queue.size(); ++i) {
            const auto &item = queue.at(i);
            const auto &song = item.metadata;
            const auto title = !song.customTitle.isEmpty() ? song.customTitle
                : !song.sourceName.isEmpty() ? song.sourceName : QFileInfo(item.path).completeBaseName();
            m_tracks.append({item.id, song.id, title, song.artist, song.album, song.durationMs,
                             song.playCount, song.lastPlayedAt});
        }
    }
    QString m_id;
    quint64 m_generation;
    ShuffleStrategyService::Selector m_select;
    QVector<sdk::ShuffleTrack> m_tracks;
    QVariantMap m_state;
    std::mt19937 m_random{std::random_device{}()};
    ShuffleBagStrategy m_fallback;
};
} // namespace

ShuffleStrategyService::ShuffleStrategyService(QString preferred, Saver save)
    : m_preferred(preferred.isEmpty() ? QStringLiteral("shuffle_bag") : std::move(preferred)),
      m_save(std::move(save)) {}

QString ShuffleStrategyService::effective() const {
    for (const auto &item : m_catalogue)
        if (item.id == m_preferred) return m_preferred;
    return QStringLiteral("shuffle_bag");
}
quint64 ShuffleStrategyService::generation() const {
    for (const auto &item : m_catalogue)
        if (item.id == m_preferred) return item.generation;
    return 0;
}
void ShuffleStrategyService::setCatalogue(QList<ShuffleStrategyDescriptor> catalogue, Selector select) {
    const auto previous = effective();
    const auto previousGeneration = generation();
    const bool changedCatalogue = m_catalogue != catalogue;
    m_catalogue = std::move(catalogue);
    m_select = std::move(select);
    if (previous != effective() || previousGeneration != generation()) emit strategyChanged();
    if (changedCatalogue) emit changed();
}
Result<void> ShuffleStrategyService::select(const QString &id) {
    bool available = id == "shuffle_bag";
    for (const auto &item : m_catalogue) available = available || item.id == id;
    if (!available) return failure("Shuffle strategy unavailable: " + id, ErrorCode::Unavailable);
    if (id == m_preferred) return {};
    if (m_save && !m_save(id)) return failure("Unable to save shuffle strategy", ErrorCode::Storage);
    m_preferred = id;
    emit strategyChanged();
    emit changed();
    return {};
}
QVariantMap ShuffleStrategyService::status() const {
    QVariantList strategies{QVariantMap{{"id", "shuffle_bag"}, {"name", "Shuffle bag"}}};
    for (const auto &item : m_catalogue)
        strategies.append(QVariantMap{{"id", item.id}, {"name", item.name}});
    return {{"shuffle_strategies", strategies}, {"shuffle_strategy", m_preferred},
            {"effective_shuffle_strategy", effective()}};
}
std::unique_ptr<IShuffleStrategy> ShuffleStrategyService::createStrategy() const {
    if (effective() == "shuffle_bag") return std::make_unique<ShuffleBagStrategy>();
    return std::make_unique<ExtensionShuffleStrategy>(m_preferred, generation(), m_select);
}
} // namespace nekotune
