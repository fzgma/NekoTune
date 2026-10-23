#pragma once
#include "domain/playback/shuffle_strategy_descriptor.h"
#include <nekotune/sdk/shuffle_provider.h>
#include <QHash>
#include <QMap>
#include <QObject>

namespace nekotune {
class CapabilityHost;
class ShuffleStrategies final : public QObject {
    Q_OBJECT
  public:
    explicit ShuffleStrategies(CapabilityHost &host, QObject *parent = nullptr);
    void add(const QString &extensionId, const QString &localId, sdk::IShuffleProvider *provider, QString name);
    void remove(const QString &extensionId, const QString &localId);
    void removeExtension(const QString &extensionId);
    QList<ShuffleStrategyDescriptor> strategies() const;
    Result<sdk::ShuffleSelection> select(const QString &id, quint64 generation, const sdk::ShuffleRequest &request);
  signals:
    void catalogueChanged();

  private:
    struct Strategy {
        sdk::IShuffleProvider *provider = nullptr;
        QString name;
        quint64 generation = 0;
        bool failed = false;
    };
    CapabilityHost &m_host;
    QHash<QString, QMap<QString, Strategy>> m_strategies;
    quint64 m_generation = 0;
};
} // namespace nekotune
