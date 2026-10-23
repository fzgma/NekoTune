#pragma once
#include "domain/result.h"
#include <QVariantMap>

namespace nekotune {
/// Public view of manifest contributions and runtime UI registrations.
class ContributionCatalog {
  public:
    virtual ~ContributionCatalog() = default;
    /// Contributions of running extensions for one UI kind.
    virtual QVariantList contributions(const QString &kind) const = 0;
    /// Every theme of every installed extension; available marks running ones.
    virtual QVariantList themes() const = 0;
    struct ThemePackage {
        QVariantMap theme;
        QString directory;
        QVariantMap manifest;
    };
    virtual Result<ThemePackage> themePackage(const QString &contributionId) const = 0;
};
} // namespace nekotune
