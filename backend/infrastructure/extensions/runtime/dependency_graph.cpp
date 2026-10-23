#include "infrastructure/extensions/runtime/dependency_graph.h"
#include "infrastructure/extensions/package/semver.h"
#include <functional>

namespace nekotune {
Result<QStringList> dependencyOrder(const QString &id, const DependencyNodes &nodes) {
    QStringList result, visiting;
    std::function<Result<void>(const QString &)> visit = [&](const QString &current) -> Result<void> {
        if (visiting.contains(current))
            return failure("Dependency cycle: " + (visiting + QStringList{current}).join(" → "));
        if (result.contains(current))
            return {};
        if (!nodes.contains(current))
            return failure("Missing extension: " + current);
        visiting.append(current);
        const auto dependencies = nodes[current].dependencies;
        for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it) {
            const auto range = it.value();
            if (!nodes.contains(it.key()) ||
                !semver::satisfies(nodes[it.key()].version, range))
                return failure(QStringLiteral("Dependency %1 must satisfy %2").arg(it.key(), range));
            if (auto nested = visit(it.key()); !nested)
                return nested;
        }
        visiting.removeLast();
        result.append(current);
        return {};
    };
    if (auto visited = visit(id); !visited)
        return visited.error();
    return result;
}
QStringList dependentsOf(const QString &id, const QStringList &order, const DependencyNodes &nodes) {
    QStringList found{id};
    qsizetype size;
    do {
        size = found.size();
        for (const auto &candidate : order) {
            if (found.contains(candidate))
                continue;
            const auto dependencies = nodes[candidate].dependencies;
            for (const auto &dependency : found)
                if (dependencies.contains(dependency)) {
                    found.append(candidate);
                    break;
                }
        }
    } while (size != found.size());
    return found;
}

} // namespace nekotune
