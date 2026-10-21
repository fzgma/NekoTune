#include "infrastructure/library/library_file_io.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryFile>
namespace nekotune::LibraryFileIO {
Result<QString> hashFile(const QString &path, const std::atomic_bool *cancel) {
    QFile input(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!input.open(QIODevice::ReadOnly))
        return failure(input.errorString(), ErrorCode::Io);
    while (!input.atEnd()) {
        if (cancel && cancel->load())
            return failure("Cancelled", ErrorCode::Cancelled);
        const auto data = input.read(1024 * 1024);
        if (input.error() != QFileDevice::NoError)
            return failure(input.errorString(), ErrorCode::Io);
        hash.addData(data);
    }
    return QString::fromLatin1(hash.result().toHex());
}
Result<QString> copyFile(const QString &source, const QString &target, const std::atomic_bool *cancel,
                         Progress progress) {
    // Windows may report the link's own size/timestamps. Check the target whose bytes we copy.
    const QFileInfo before(QFileInfo(source).canonicalFilePath());
    if (!before.isFile() || QFileInfo::exists(target) || QFileInfo(target).isSymLink())
        return failure("Invalid source or destination already exists", ErrorCode::Io);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return failure("Cannot create destination directory", ErrorCode::Io);
    QFile input(before.absoluteFilePath());
    QTemporaryFile output(target + ".copy-XXXXXX");
    if (!input.open(QIODevice::ReadOnly) || !output.open())
        return failure("Cannot open source or destination", ErrorCode::Io);
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        if (cancel && cancel->load())
            return failure("Cancelled", ErrorCode::Cancelled);
        const auto data = input.read(1024 * 1024);
        if (input.error() != QFileDevice::NoError || output.write(data) != data.size())
            return failure("Audio copy failed (read error or disk full)", ErrorCode::Io);
        hash.addData(data);
        if (progress)
            progress(data.size());
    }
    const QFileInfo after(QFileInfo(source).canonicalFilePath());
    if (before.absoluteFilePath() != after.absoluteFilePath() ||
        before.size() != after.size() || before.lastModified() != after.lastModified() ||
        output.size() != before.size())
        return failure("Source changed during copy", ErrorCode::Io);
    if (!output.flush())
        return failure(output.errorString(), ErrorCode::Io);
    output.close();
    input.close();
    if (cancel && cancel->load())
        return failure("Cancelled", ErrorCode::Cancelled);
    // close() retains QTemporaryFile's native handle; its rename() releases it on Windows.
    if (!output.rename(target))
        return failure("Cannot publish copied file: " + output.errorString(), ErrorCode::Io);
    output.setAutoRemove(false);
    return QString::fromLatin1(hash.result().toHex());
}
Result<void> writeJson(const QString &path, const QJsonObject &object) {
    if (QFileInfo(path).isSymLink() || !QDir().mkpath(QFileInfo(path).absolutePath()))
        return failure("Invalid task path", ErrorCode::Io);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return failure(file.errorString(), ErrorCode::Io);
    return {};
}
Result<QJsonObject> readJson(const QString &path) {
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
        return failure("Cannot read manifest", ErrorCode::Io);
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return failure("Invalid manifest");
    return doc.object();
}
bool within(const QString &root, const QString &path) {
    const auto clean = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const auto value = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return value == clean || value.startsWith(clean + '/');
}
} // namespace nekotune::LibraryFileIO
