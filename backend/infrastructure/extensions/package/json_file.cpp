#include "infrastructure/extensions/package/json_file.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace nekotune {
QVariant readJsonFile(const QString &path, const QVariant &fallback) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fallback;
    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isNull() ? fallback : document.toVariant();
}
Result<void> writeJsonFile(const QString &path, const QVariant &value) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failure(file.errorString(), ErrorCode::Io);
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument::fromVariant(value).toJson());
    if (!file.commit())
        return failure(file.errorString(), ErrorCode::Io);
    return {};
}
} // namespace nekotune
