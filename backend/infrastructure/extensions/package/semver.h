#pragma once
#include <QStringList>
#include <optional>

namespace nekotune::semver {
/// Strict SemVer 2.0 version; build metadata is ignored for ordering.
struct Version {
    quint64 major = 0, minor = 0, patch = 0;
    QStringList prerelease;
};
std::optional<Version> parse(const QString &text);
int compare(const Version &left, const Version &right);
/// npm-style ranges: comparators (<, <=, >, >=, =), ^, ~, x/* wildcards, hyphen ranges and ||.
/// Prerelease versions only satisfy comparators that name the same major.minor.patch prerelease.
bool validRange(const QString &range);
bool satisfies(const QString &version, const QString &range);
} // namespace nekotune::semver
