#pragma once
#include "domain/result.h"
#include <QVariant>

namespace nekotune {
/// fallback when the file is missing or is not valid JSON.
QVariant readJsonFile(const QString &path, const QVariant &fallback);
/// Atomic owner-only write; creates the parent directory.
Result<void> writeJsonFile(const QString &path, const QVariant &value);
} // namespace nekotune
