#pragma once
#include "domain/extensions/lyrics_source_catalog.h"
#include "domain/lyrics/lyrics_provider.h"
#include <QPointer>
namespace nekotune {
class ExtensionLyricsProvider final : public LyricsProvider {
    Q_OBJECT
  public:
    ExtensionLyricsProvider(ILyricsSourceCatalog &catalog, QString id, QString name)
        : m_catalog(&catalog), m_id(std::move(id)), m_name(std::move(name)) {}
    LyricsSource descriptor() const override { return {m_id, m_name, true, true}; }
    void request(const LyricsQuery &, quint64 token, bool search) override;
    void choose(const LyricsCandidate &, quint64 token) override;
    void cancel() override { ++m_generation; }

  private:
    void call(const QString &operation, const QVariantMap &, quint64 token, bool candidates);
    LyricsDocument document(const QVariantMap &) const;
    QPointer<ILyricsSourceCatalog> m_catalog;
    QString m_id, m_name;
    LyricsQuery m_query;
    quint64 m_generation = 0;
};
} // namespace nekotune
