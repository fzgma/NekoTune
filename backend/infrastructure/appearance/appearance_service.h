#pragma once
#include "domain/appearance/appearance_backend.h"
#include <QStringList>

namespace nekotune {
class ContributionCatalog;
/// Theme, layout and slot selections, stored in config/extensions/appearance.json.
class AppearanceService final : public IAppearanceBackend {
    Q_OBJECT
  public:
    AppearanceService(ContributionCatalog &catalog, QString path, QObject *parent = nullptr);
    void request(const QString &method, const QVariantMap &params, Completion done) override;
    QVariantMap selections() const override { return m_selections; }
    /// One-time migration; an existing appearance.json takes precedence over registry.json.
    Result<void> migrate(const QVariantMap &legacy);
    QStringList selectedOwners() const;
    Result<void> forgetThemePackage(const QString &extensionId);

  private:
    Result<void> validate(const QString &slot, const QString &contribution) const;
    Result<QVariantMap> select(const QVariantMap &params);
    Result<QVariantMap> exportTheme(const QVariantMap &params) const;
    Result<void> save();
    ContributionCatalog &m_catalog;
    QString m_path;
    QVariantMap m_selections;
};
} // namespace nekotune
