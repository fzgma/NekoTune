#include "infrastructure/library/music_directory.h"
#include "app_paths.h"
#include "domain/lyrics/krc_parser.h"
#include "infrastructure/library/library_file_io.h"
#include "storage/resource_repository.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
namespace nekotune {
namespace {
class Removal final : public IManagedFileRemoval {
  public:
    QVector<QPair<QString, QString>> files;
    QString root, journal, token;
    QJsonArray objects;
    bool committed = false;
    Result<void> save() {
        return LibraryFileIO::writeJson(
            journal, {{"format", "nekotune-removal"}, {"token", token}, {"objects", objects}});
    }
    ~Removal() override {
        if (committed)
            return;
        bool restored = true;
        for (auto it = files.crbegin(); it != files.crend(); ++it)
            if (QFileInfo::exists(it->second) && !QFile::rename(it->second, it->first))
                restored = false;
        if (restored)
            QFile::remove(journal);
    }
    QStringList commit() override {
        committed = true;
        QStringList errors;
        for (const auto &entry : files)
            if (!QFile::remove(entry.second))
                errors << entry.second;
        if (errors.isEmpty())
            QFile::remove(journal);
        return errors;
    }
};
void recoverRemovals(DatabaseSession &db, const QString &root) {
    QDir staging(QDir(root).filePath(".staging"));
    for (const auto &name : staging.entryList({"remove-*.json"}, QDir::Files)) {
        auto parsed = LibraryFileIO::readJson(staging.filePath(name));
        if (!parsed || parsed.value()["format"] != "nekotune-removal")
            continue;
        const auto token = parsed.value()["token"].toString();
        if (!QRegularExpression("^[a-f0-9-]{36}$").match(token).hasMatch() ||
            name != "remove-" + token + ".json")
            continue;
        bool recovered = true;
        for (const auto &v : parsed.value()["objects"].toArray()) {
            const auto object = v.toObject();
            const auto relative = object["path"].toString(), hash = object["hash"].toString();
            if (!QRegularExpression("^(media|artwork)/[a-f0-9]{2}/[a-f0-9]{64}\\.[a-z0-9]{1,12}$")
                     .match(relative)
                     .hasMatch() ||
                QFileInfo(relative).completeBaseName() != hash) {
                recovered = false;
                break;
            }
            const auto path = ResourceRepository::safePath(root, relative),
                       staged = ResourceRepository::safePath(root, relative + ".removing-" + token);
            if (path.isEmpty() || staged.isEmpty()) {
                recovered = false;
                break;
            }
            QSqlQuery q(db.database());
            q.prepare(relative.startsWith("media/") ? "SELECT 1 FROM audio_objects WHERE hash=?"
                                                    : "SELECT 1 FROM artwork_objects WHERE hash=?");
            q.addBindValue(hash);
            if (!q.exec()) {
                recovered = false;
                break;
            }
            const bool registered = q.next();
            if (!QFileInfo::exists(staged))
                continue;
            if (registered) {
                if (!QFileInfo::exists(path) && !QFile::rename(staged, path))
                    recovered = false;
                else if (QFileInfo::exists(path) && QFileInfo::exists(staged))
                    recovered = false;
            } else if (!QFile::remove(staged))
                recovered = false;
        }
        if (recovered)
            QFile::remove(staging.filePath(name));
    }
}
} // namespace
MusicDirectory::MusicDirectory(DatabaseSession &db, const QString &directory)
    : m_database(db), m_directory(directory.isEmpty() ? AppPaths::musicDirectory()
                                                      : QFileInfo(directory).absoluteFilePath()) {
    db.setLibraryRoot(m_directory);
    if (db.isReady())
        recoverRemovals(db, m_directory);
}
Result<QString> MusicDirectory::reserveDownload(const QString &, const QString &) {
    const auto folder =
        QDir(m_directory).filePath(".staging/download-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(folder))
        return failure("Cannot create download staging directory", ErrorCode::Io);
    auto saved = LibraryFileIO::writeJson(QDir(folder).filePath("download.json"), {{"stage", "downloading"}});
    if (!saved)
        return saved.error();
    return QDir(folder).filePath("audio");
}
Result<ImportedFile> MusicDirectory::manage(const ImportedFile &source) {
    if (!m_database.isReady())
        return failure(m_database.errorString(), ErrorCode::Storage);
    if (!QRegularExpression("^[a-f0-9]{64}$").match(source.hash).hasMatch())
        return failure("Invalid audio hash");
    ImportedFile result = source;
    if (result.sidecars.contains("krc")) {
        auto decoded = KrcParser::read(result.sidecars["krc"]);
        if (!decoded || KrcParser::parse(*decoded).isEmpty())
            return failure("Invalid KRC sidecar");
    }
    const auto extension = QFileInfo(source.path).suffix().toLower();
    if (!QRegularExpression("^[a-z0-9]{1,12}$").match(extension).hasMatch())
        return failure("Invalid audio extension");
    QSqlQuery q(m_database.database());
    q.prepare("SELECT relative_path,size,mtime FROM audio_objects WHERE hash=?");
    q.addBindValue(source.hash);
    if (!q.exec())
        return failure(q.lastError().text(), ErrorCode::Storage);
    const bool exists = q.next();
    const auto relative =
        exists ? q.value(0).toString() : "media/" + source.hash.left(2) + '/' + source.hash + '.' + extension;
    const auto target = ResourceRepository::safePath(m_directory, relative);
    if (target.isEmpty() || !relative.startsWith("media/" + source.hash.left(2) + '/') ||
        QFileInfo(relative).completeBaseName() != source.hash)
        return failure("Unsafe audio object path", ErrorCode::Io);
    const QFileInfo present(target);
    if (present.exists()) {
        if (!present.isFile())
            return failure("Audio object is not a regular file", ErrorCode::Io);
        if (!exists || present.size() != q.value(1).toLongLong() ||
            present.lastModified().toMSecsSinceEpoch() != q.value(2).toLongLong()) {
            auto hash = LibraryFileIO::hashFile(target);
            if (!hash || hash.value() != source.hash)
                return failure("Existing audio object failed verification", ErrorCode::Io);
        }
    } else {
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return failure("Cannot create media directory", ErrorCode::Io);
        if (!source.stagingDirectory.isEmpty() &&
            LibraryFileIO::within(QDir(m_directory).filePath(".staging"), source.path)) {
            auto journal = LibraryFileIO::writeJson(QDir(source.stagingDirectory).filePath("import.json"),
                                                    {{"stage", "publishing"},
                                                     {"hash", source.hash},
                                                     {"relative_path", relative},
                                                     {"source", source.sourcePath}});
            if (!journal)
                return journal.error();
            if (!QFile::rename(source.path, target))
                return failure("Cannot publish audio object", ErrorCode::Io);
        } else {
            auto copied = LibraryFileIO::copyFile(source.path, target);
            if (!copied)
                return copied.error();
            if (copied.value() != source.hash) {
                QFile::remove(target);
                return failure("Source content changed", ErrorCode::Io);
            }
        }
    }
    const QFileInfo published(target);
    q.prepare("INSERT INTO audio_objects(hash,relative_path,size,mtime,state,retained) "
              "VALUES(?,?,?,?,'ready',0) "
              "ON CONFLICT(hash) DO UPDATE SET "
              "size=excluded.size,mtime=excluded.mtime,state='ready'");
    q.addBindValue(source.hash);
    q.addBindValue(relative);
    q.addBindValue(published.size());
    q.addBindValue(published.lastModified().toMSecsSinceEpoch());
    if (!q.exec())
        return failure(q.lastError().text(), ErrorCode::Storage);
    result.path = target;
    auto assets = publishArtwork(result);
    if (!assets)
        return assets.error();
    if (!result.stagingDirectory.isEmpty()) {
        auto journal = LibraryFileIO::writeJson(QDir(result.stagingDirectory).filePath("import.json"),
                                                {{"stage", "published"},
                                                 {"hash", source.hash},
                                                 {"relative_path", relative},
                                                 {"source", source.sourcePath}});
        if (!journal)
            return journal.error();
    }
    return result;
}
Result<void> MusicDirectory::publishArtwork(ImportedFile &file) {
    for (const auto &suffix : {"jpg", "jpeg", "png", "webp"}) {
        if (!file.sidecars.contains(suffix))
            continue;
        auto bytes = file.sidecars.value(suffix);
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const auto size = reader.size();
        const auto format = reader.format();
        if (bytes.size() > 5 * 1024 * 1024 || !size.isValid() || size.width() > 4096 ||
            size.height() > 4096 || (format != "jpeg" && format != "png" && format != "webp") ||
            reader.read().isNull())
            return failure("Invalid cover image");
        const auto hash =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const auto ext = format == "jpeg" ? QString("jpg") : QString::fromLatin1(format);
        const auto relative = "artwork/" + hash.left(2) + '/' + hash + '.' + ext;
        const auto target = ResourceRepository::safePath(m_directory, relative);
        if (target.isEmpty() || !QDir().mkpath(QFileInfo(target).absolutePath()))
            return failure("Unsafe artwork path", ErrorCode::Io);
        if (!QFileInfo::exists(target)) {
            QSaveFile output(target);
            output.setDirectWriteFallback(false);
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
                return failure("Cannot save artwork", ErrorCode::Io);
        } else {
            const auto existing = LibraryFileIO::hashFile(target);
            if (!existing || existing.value() != hash)
                return failure("Existing artwork failed verification", ErrorCode::Io);
        }
        QSqlQuery q(m_database.database());
        const bool alternate = file.sidecars.contains("cover_hash");
        q.prepare("INSERT INTO artwork_objects(hash,relative_path,size,retained,mtime) "
                  "VALUES(?,?,?,?,?) ON CONFLICT(hash) DO UPDATE SET "
                  "retained=max(retained,excluded.retained),mtime=excluded.mtime,state='ready'");
        q.addBindValue(hash);
        q.addBindValue(relative);
        q.addBindValue(bytes.size());
        q.addBindValue(alternate ? 1 : 0);
        q.addBindValue(QFileInfo(target).lastModified().toMSecsSinceEpoch());
        if (!q.exec())
            return failure(q.lastError().text(), ErrorCode::Storage);
        if (!alternate)
            file.sidecars["cover_hash"] = hash.toLatin1();
    }
    return {};
}
Result<bool> MusicDirectory::ignored(const QString &hash) const {
    QSqlQuery q(m_database.database());
    q.prepare("SELECT 1 FROM scan_ignored WHERE hash=?");
    q.addBindValue(hash);
    if (!q.exec())
        return failure(q.lastError().text(), ErrorCode::Storage);
    return q.next();
}
bool MusicDirectory::containsSong(const QString &hash) const {
    QSqlQuery q(m_database.database());
    q.prepare("SELECT 1 FROM songs WHERE hash=? UNION SELECT 1 FROM song_assets "
              "WHERE audio_hash=?");
    q.addBindValue(hash);
    q.addBindValue(hash);
    return q.exec() && q.next();
}
QString MusicDirectory::baseFor(const QString &hash) const {
    QSqlQuery q(m_database.database());
    q.prepare("SELECT id FROM managed_resources WHERE hash=?");
    q.addBindValue(hash);
    if (!q.exec() || !q.next())
        return {};
    const auto number = QString::number(q.value(0).toLongLong()).rightJustified(6, '0');
    return QDir(m_directory).filePath(number + '/' + number);
}
Result<std::unique_ptr<IManagedFileRemoval>> MusicDirectory::stageRemoval(const QStringList &keys) {
    if (m_pinned)
        return failure("Library files are in use by another task", ErrorCode::Unavailable);
    auto removal = std::make_unique<Removal>();
    QSqlQuery q(m_database.database());
    removal->root = m_directory;
    removal->token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    removal->journal = QDir(m_directory).filePath(".staging/remove-" + removal->token + ".json");
    for (const auto &key : keys) {
        const bool cover = key.startsWith("cover/");
        const auto hash = cover ? key.mid(6) : key;
        const auto table = cover ? QString("artwork_objects") : QString("audio_objects"),
                   field = cover ? QString("cover_hash") : QString("audio_hash");
        q.prepare("SELECT relative_path FROM " + table +
                  " WHERE hash=? AND retained=0 AND NOT EXISTS(SELECT 1 FROM "
                  "song_assets WHERE " +
                  field + "=?)");
        q.addBindValue(hash);
        q.addBindValue(hash);
        if (!q.exec())
            return failure(q.lastError().text(), ErrorCode::Storage);
        if (!q.next())
            continue;
        const auto relative = q.value(0).toString(),
                   path = ResourceRepository::safePath(m_directory, relative);
        if (path.isEmpty() ||
            !relative.startsWith((cover ? QString("artwork/") : QString("media/")) + hash.left(2) + '/') ||
            QFileInfo(relative).completeBaseName() != hash)
            return failure("Unsafe cleanup path", ErrorCode::Io);
        if (QFileInfo::exists(path)) {
            const auto staged = path + ".removing-" + removal->token;
            removal->files.append({path, staged});
            removal->objects.append(QJsonObject{{"path", relative}, {"hash", hash}});
            auto saved = removal->save();
            if (!saved)
                return saved.error();
            if (!QFile::rename(path, staged))
                return failure("Cannot stage object removal", ErrorCode::Io);
        }
        q.prepare("DELETE FROM " + table + " WHERE hash=?");
        q.addBindValue(hash);
        if (!q.exec())
            return failure(q.lastError().text(), ErrorCode::Storage);
    }
    return std::unique_ptr<IManagedFileRemoval>(std::move(removal));
}
} // namespace nekotune
