#pragma once
#include "domain/result.h"
#include <QJsonObject>
#include <atomic>
#include <functional>
namespace nekotune::LibraryFileIO {
using Progress = std::function<void(qint64)>;
Result<QString> hashFile(const QString &path, const std::atomic_bool *cancel = nullptr);
Result<QString> copyFile(const QString &source, const QString &target,
                         const std::atomic_bool *cancel = nullptr, Progress progress = {});
Result<void> writeJson(const QString &path, const QJsonObject &object);
Result<QJsonObject> readJson(const QString &path);
bool within(const QString &root, const QString &path);
} // namespace nekotune::LibraryFileIO
