#pragma once
#include "domain/playback/shuffle_strategy.h"
#include "domain/playback/shuffle_strategy_descriptor.h"
#include <nekotune/sdk/shuffle_provider.h>
#include <QObject>
#include <functional>

namespace nekotune {
/// Owns user preference, not extension objects. All calls occur on the backend thread.
class ShuffleStrategyService final : public QObject {
    Q_OBJECT
  public:
    using Selector = std::function<Result<sdk::ShuffleSelection>(const QString &, quint64,
                                                                const sdk::ShuffleRequest &)>;
    using Saver = std::function<bool(const QString &)>;
    explicit ShuffleStrategyService(QString preferred = {}, Saver save = {});
    void setCatalogue(QList<ShuffleStrategyDescriptor> catalogue, Selector select);
    Result<void> select(const QString &id);
    QString preferred() const { return m_preferred; }
    QString effective() const;
    QVariantMap status() const;
    std::unique_ptr<IShuffleStrategy> createStrategy() const;
  signals:
    void strategyChanged();
    void changed();
  private:
    quint64 generation() const;
    QString m_preferred;
    Saver m_save;
    QList<ShuffleStrategyDescriptor> m_catalogue;
    Selector m_select;
};
} // namespace nekotune
