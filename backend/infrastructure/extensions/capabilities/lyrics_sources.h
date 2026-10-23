#pragma once
#include "domain/extensions/lyrics_source_catalog.h"
#include <nekotune/sdk/providers.h>
#include <QHash>
#include <QMap>

namespace nekotune {
class CapabilityHost;
class LyricsSources final : public ILyricsSourceCatalog {
    Q_OBJECT
  public:
    explicit LyricsSources(CapabilityHost &host, QObject *parent = nullptr);
    void add(const QString &extensionId, const QString &localId, sdk::ILyricsProvider *provider,
             QVariantMap descriptor);
    void remove(const QString &extensionId, const QString &localId);
    void removeExtension(const QString &extensionId);
    void notifyChanged() { emit sourcesChanged(); }
    QVariantList sources() const override;
    void search(const QString &source, const QVariantMap &query, Reply done) override;
    void resolve(const QString &source, const QVariantMap &candidate, Reply done) override;

  private:
    struct Source {
        sdk::ILyricsProvider *provider = nullptr;
        QVariantMap descriptor;
    };
    void call(const QString &source, const QVariantMap &params, bool search, Reply done);
    CapabilityHost &m_host;
    QHash<QString, QMap<QString, Source>> m_sources;
};
} // namespace nekotune
