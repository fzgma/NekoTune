#pragma once
#include "domain/result.h"
#include <QHash>
#include <QMap>
#include <QStringList>
#include <QVariantMap>

namespace nekotune {
/// Descriptors registered by one run, keyed by kind and local ID.
class RegistrationScope {
  public:
    static const QStringList &uiKinds();
    static const QStringList &kinds();
    /// Validates kind and ID, rejects IDs already contributed by the manifest or registered, keeps
    /// sources inside directory and validates themes; then records the descriptor with id,
    /// localId and extensionId filled in.
    Result<void> add(const QString &kind, const QString &localId, QVariantMap descriptor,
                     const QString &extensionId, const QString &directory, const QVariantMap &contributes);
    void remove(const QString &kind, const QString &localId);
    bool contains(const QString &kind, const QString &localId) const;
    QVariantMap descriptor(const QString &kind, const QString &localId) const;
    /// Descriptors of one kind ordered by local ID.
    QMap<QString, QVariantMap> list(const QString &kind) const;
    void clear();

  private:
    QHash<QString, QMap<QString, QVariantMap>> m_items;
};
} // namespace nekotune
