#pragma once
#include "domain/result.h"
#include <QHash>
#include <QMap>
#include <QStringList>

namespace nekotune {
struct DependencyNode {
    QString version;
    /// Extension ID → SemVer range.
    QMap<QString, QString> dependencies;
};
using DependencyNodes = QHash<QString, DependencyNode>;

/// id and everything it needs, dependencies first. Fails on missing extensions, unsatisfied
/// ranges and cycles.
Result<QStringList> dependencyOrder(const QString &id, const DependencyNodes &nodes);
/// id followed by every extension that depends on it, directly or transitively, in the
/// installation order given by order.
QStringList dependentsOf(const QString &id, const QStringList &order, const DependencyNodes &nodes);
} // namespace nekotune
