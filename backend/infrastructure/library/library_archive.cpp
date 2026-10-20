#include "infrastructure/library/library_archive.h"
#include "domain/lyrics/krc_parser.h"
#include "storage/resource_repository.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <zlib.h>
namespace nekotune::LibraryArchive {
namespace {
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
struct Connection {
    QString name = "library-archive-" + token();
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
    explicit Connection(const QString &path, bool readOnly = true) {
        db.setDatabaseName(path);
        db.setConnectOptions(readOnly ? "QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"
                                      : "QSQLITE_BUSY_TIMEOUT=5000");
        db.open();
    }
    ~Connection() {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
};
Result<void> validateDb(QSqlDatabase db) {
    if (!db.isOpen())
        return failure(db.lastError().text(), ErrorCode::Storage);
    QSqlQuery q(db);
    if (!q.exec("PRAGMA quick_check") || !q.next() || q.value(0).toString() != "ok")
        return failure("Database integrity check failed");
    q.finish();
    if (!q.exec("PRAGMA foreign_key_check") || q.next())
        return failure("Database foreign key check failed");
    return {};
}
Result<void> writeBytes(const QString &path, const QByteArray &bytes) {
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        return failure("Export destination exists", ErrorCode::Io);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return failure("Cannot write export file", ErrorCode::Io);
    return {};
}
QString safeName(QString value) {
    value = value.normalized(QString::NormalizationForm_C);
    value.replace(QRegularExpression("[\\x00-\\x1f<>:\"/\\\\|?*]"), "_");
    value = value.trimmed();
    // Leave room for an ID suffix and extension on filesystems with a 255-byte
    // component limit, including names composed entirely of Chinese or emoji.
    while (value.size() > 120 || value.toUtf8().size() > 180)
        value.chop(value.back().isLowSurrogate() ? 2 : 1);
    while (value.endsWith('.') || value.endsWith(' '))
        value.chop(1);
    if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                           QRegularExpression::CaseInsensitiveOption)
            .match(value)
            .hasMatch())
        value.prepend('_');
    return value;
}
QByteArray krcBytes(const QString &text) {
    const auto raw = text.toUtf8();
    uLongf length = compressBound(raw.size());
    QByteArray data(length, Qt::Uninitialized);
    if (compress2(reinterpret_cast<Bytef *>(data.data()), &length,
                  reinterpret_cast<const Bytef *>(raw.constData()), raw.size(), Z_BEST_SPEED) != Z_OK)
        return {};
    data.resize(length);
    const unsigned char key[] = {0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
                                 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
    for (qsizetype i = 0; i < data.size(); ++i)
        data[i] = char(static_cast<unsigned char>(data[i]) ^ key[i % 16]);
    return "krc1" + data;
}
QString canonicalDestination(const QString &path) {
    QFileInfo info(path);
    QStringList tail;
    while (!info.exists()) {
        tail.prepend(info.fileName());
        const auto parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            return {};
        info = QFileInfo(parent);
    }
    auto result = info.canonicalFilePath();
    for (const auto &part : tail)
        result = QDir(result).filePath(part);
    return result;
}
QString journalPath(const QString &root) { return QDir(root).filePath("config/restore-journal.json"); }
bool removeEntry(const QString &path) {
    QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;
    return info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path);
}
Result<void> validateJournal(const QJsonObject &j, const QString &root, const QString &database) {
    const auto id = j["id"].toString();
    if (!QRegularExpression("^[a-f0-9-]{36}$").match(id).hasMatch())
        return failure("Invalid restore journal ID");
    const auto actions = j["actions"].toArray();
    if (actions.size() != 3)
        return failure("Invalid restore journal");
    for (int i = 0; i < actions.size(); ++i) {
        const auto a = actions[i].toObject();
        const auto item = i == 0 ? QString("media") : QString("artwork");
        const auto expected = i == 2 ? database : QDir(root).filePath(item);
        const auto staged = i == 2 ? database + ".restore-new-" + id
                                   : QDir(root).filePath(".staging/restore-" + id + "/" + item);
        const auto old =
            i == 2 ? database + ".restore-old-" + id : QDir(root).filePath(".rollback/" + id + "/" + item);
        if (a["target"] != expected || a["new"] != staged || a["old"] != old ||
            QFileInfo(expected).isSymLink() || QFileInfo(staged).isSymLink() || QFileInfo(old).isSymLink())
            return failure("Unsafe restore journal path");
        if (i != 2 && (ResourceRepository::safePath(root, item).isEmpty() ||
                       ResourceRepository::safePath(root, ".staging/restore-" + id + '/' + item).isEmpty() ||
                       ResourceRepository::safePath(root, ".rollback/" + id + '/' + item).isEmpty()))
            return failure("Linked restore directory is not allowed");
    }
    return {};
}
} // namespace
Result<void> snapshotDatabase(const QString &source, const QString &target) {
    Connection c(source);
    if (!c.db.isOpen())
        return failure(c.db.lastError().text(), ErrorCode::Storage);
    QSqlQuery q(c.db);
    q.prepare("VACUUM INTO ?");
    q.addBindValue(target);
    if (!q.exec())
        return failure(q.lastError().text(), ErrorCode::Storage);
    q.finish();
    Connection snapshot(target);
    return validateDb(snapshot.db);
}
Result<QJsonObject> exportSongs(const QVector<ExportSong> &songs, const QString &destination, bool playlist,
                                const std::atomic_bool &cancel, Progress progress) {
    if (!QDir().mkpath(destination))
        return failure("Cannot create export destination", ErrorCode::Io);
    QTemporaryDir temp(QDir(destination).filePath(".nekotune-export-XXXXXX"));
    if (!temp.isValid())
        return failure("Cannot stage export", ErrorCode::Io);
    int written = 0, skipped = 0, failed = 0;
    qint64 bytes = 0;
    QJsonArray errors;
    QSet<QString> names;
    QStringList m3u{"#EXTM3U"};
    for (const auto &song : songs) {
        if (cancel.load())
            return failure("Cancelled", ErrorCode::Cancelled);
        if (song.audio.isEmpty() || !QFileInfo(song.audio).isFile()) {
            ++skipped;
            errors.append(QJsonObject{{"song_id", song.id}, {"message", "Audio unavailable"}});
            continue;
        }
        auto name = safeName(song.title.isEmpty()    ? song.sourceName
                             : song.artist.isEmpty() ? song.title
                                                     : song.artist + " - " + song.title);
        if (name.isEmpty())
            name = QString::number(song.id);
        const auto original = name;
        int duplicate = 0;
        while (names.contains(name.toCaseFolded()))
            name = original + " [" + QString::number(song.id) +
                   (duplicate++ ? "-" + QString::number(duplicate) : QString()) + "]";
        names.insert(name.toCaseFolded());
        const auto filename = name + '.' + QFileInfo(song.audio).suffix();
        auto copy = LibraryFileIO::copyFile(song.audio, temp.filePath(filename), &cancel, [&](qint64 n) {
            bytes += n;
            if (progress)
                progress(bytes, written);
        });
        if (!copy) {
            if (copy.error().code == ErrorCode::Cancelled)
                return copy.error();
            ++failed;
            errors.append(QJsonObject{{"song_id", song.id}, {"message", copy.error().message}});
            continue;
        }
        bool ok = true;
        auto write = [&](const QString &suffix, const QByteArray &data) {
            if (!writeBytes(temp.filePath(name + suffix), data))
                ok = false;
        };
        if (song.lyrics) {
            const auto &d = *song.lyrics;
            if (!d.krcLyrics.isEmpty())
                write(".krc", song.rawKrc.isEmpty() ? krcBytes(d.krcLyrics) : song.rawKrc);
            if (!d.syncedLyrics.isEmpty())
                write(".lrc", d.syncedLyrics.toUtf8());
            else if (!d.plainLyrics.isEmpty())
                write(".txt", d.plainLyrics.toUtf8());
        }
        if (!song.cover.isEmpty() && QFileInfo(song.cover).isFile()) {
            auto cover = LibraryFileIO::copyFile(
                song.cover, temp.filePath(name + '.' + QFileInfo(song.cover).suffix()), &cancel);
            if (!cover)
                ok = false;
        }
        if (!ok) {
            ++failed;
            errors.append(QJsonObject{{"song_id", song.id},
                                      {"message", "Audio exported; some companion files failed"}});
        } else
            ++written;
        m3u << QString("#EXTINF:%1,%2").arg(song.duration / 1000).arg(name) << "./" + filename;
    }
    if (playlist) {
        auto saved = writeBytes(temp.filePath("playlist.m3u8"), (m3u.join('\n') + '\n').toUtf8());
        if (!saved)
            return saved.error();
    }
    if (cancel.load())
        return failure("Cancelled", ErrorCode::Cancelled);
    const auto target = QDir(destination).filePath("NekoTune-export-" + token());
    if (!QDir().rename(temp.path(), target))
        return failure("Cannot publish export", ErrorCode::Io);
    temp.setAutoRemove(false);
    return QJsonObject{{"path", target},   {"exported", written}, {"skipped", skipped},
                       {"failed", failed}, {"errors", errors},    {"bytes", bytes}};
}
Result<QJsonObject> backup(const QString &database, const QString &root, const QString &destination,
                           const std::atomic_bool &cancel, Progress progress) {
    const auto dest = canonicalDestination(destination), canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (dest.isEmpty() || LibraryFileIO::within(canonicalRoot, dest))
        return failure("Backup destination must be outside the library");
    if (!QDir().mkpath(dest))
        return failure("Cannot create backup destination", ErrorCode::Io);
    QTemporaryDir temp(QDir(dest).filePath(".nekotune-backup-XXXXXX"));
    if (!temp.isValid())
        return failure("Cannot stage backup", ErrorCode::Io);
    auto snap = snapshotDatabase(database, temp.filePath("library.sqlite"));
    if (!snap)
        return snap.error();
    {
        // Only the private snapshot is edited; legacy ownership paths and in-flight
        // task IDs must not become executable maintenance work after restoring to
        // another root.
        Connection snapshot(temp.filePath("library.sqlite"), false);
        QSqlQuery q(snapshot.db);
        if (!q.exec("SELECT count(*) FROM songs s LEFT JOIN song_assets a ON "
                    "a.song_id=s.id LEFT JOIN storage_migrations m ON "
                    "m.song_id=s.id WHERE (s.source_provider='' AND a.audio_hash "
                    "IS NULL) OR m.state IN ('copying','failed')") ||
            !q.next() || q.value(0).toInt() != 0)
            return failure("Finish migrating the library before backing up");
        q.finish();
        for (const auto &sql : {"DELETE FROM storage_tasks", "DELETE FROM storage_migrations",
                                "DELETE FROM resource_sources", "DELETE FROM managed_resources"})
            if (!q.exec(sql))
                return failure(q.lastError().text(), ErrorCode::Storage);
    }
    QJsonArray files, missing;
    int count = 0, songs = 0;
    qint64 bytes = 0;
    {
        Connection c(temp.filePath("library.sqlite"));
        QSqlQuery q(c.db);
        if (!q.exec("SELECT count(*) FROM songs") || !q.next())
            return failure("Invalid library database");
        songs = q.value(0).toInt();
        if (!q.exec("SELECT relative_path,hash,size,state FROM audio_objects UNION ALL "
                    "SELECT relative_path,hash,size,CASE WHEN state='ready' THEN "
                    "'ready' "
                    "ELSE 'corrupt' END FROM artwork_objects"))
            return failure(q.lastError().text(), ErrorCode::Storage);
        while (q.next()) {
            if (cancel.load())
                return failure("Cancelled", ErrorCode::Cancelled);
            const auto relative = q.value(0).toString(), hash = q.value(1).toString();
            if (q.value(3).toString() == "missing") {
                missing.append(hash);
                continue;
            }
            if (q.value(3).toString() != "ready")
                return failure("Repair corrupt or missing objects before backing up: " + relative);
            const auto source = ResourceRepository::safePath(root, relative),
                       target = ResourceRepository::safePath(temp.path(), relative);
            if (source.isEmpty() || target.isEmpty())
                return failure("Unsafe object path");
            auto copied = LibraryFileIO::copyFile(source, target, &cancel, [&](qint64 n) {
                bytes += n;
                if (progress)
                    progress(bytes, count);
            });
            if (!copied)
                return copied.error();
            if (copied.value() != hash || QFileInfo(target).size() != q.value(2).toLongLong())
                return failure("Library object failed backup verification: " + relative);
            files.append(
                QJsonObject{{"path", relative}, {"sha256", hash}, {"size", q.value(2).toLongLong()}});
            ++count;
        }
    }
    auto hash = LibraryFileIO::hashFile(temp.filePath("library.sqlite"), &cancel);
    if (!hash)
        return hash.error();
    files.append(QJsonObject{{"path", "library.sqlite"},
                             {"sha256", hash.value()},
                             {"size", QFileInfo(temp.filePath("library.sqlite")).size()}});
    QJsonObject manifest{{"format", "nekotune-library"},
                         {"version", 1},
                         {"songs", songs},
                         {"files", files},
                         {"missing", missing},
                         {"created_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    auto saved = LibraryFileIO::writeJson(temp.filePath("manifest.json"), manifest);
    if (!saved)
        return saved.error();
    if (cancel.load())
        return failure("Cancelled", ErrorCode::Cancelled);
    const auto target = QDir(dest).filePath("NekoTune-backup-" + token());
    if (!QDir().rename(temp.path(), target))
        return failure("Cannot publish backup", ErrorCode::Io);
    temp.setAutoRemove(false);
    return QJsonObject{
        {"path", target}, {"songs", songs}, {"bytes", bytes}, {"files", count + 1}, {"missing", missing}};
}
Result<QJsonObject> inspectBackup(const QString &source, const std::atomic_bool &cancel, Progress progress) {
    auto parsed = LibraryFileIO::readJson(QDir(source).filePath("manifest.json"));
    if (!parsed)
        return parsed.error();
    const auto m = parsed.value();
    if (m["format"] != "nekotune-library" || m["version"] != 1 || !m["files"].isArray() ||
        !m["missing"].isArray())
        return failure("Unsupported backup format or version");
    QHash<QString, QJsonObject> files;
    qint64 bytes = 0;
    int count = 0;
    for (const auto &value : m["files"].toArray()) {
        if (cancel.load())
            return failure("Cancelled", ErrorCode::Cancelled);
        const auto item = value.toObject();
        const auto relative = item["path"].toString();
        if (relative != "library.sqlite" &&
            !QRegularExpression("^(media|artwork)/[a-f0-9]{2}/[a-f0-9]{64}\\.[a-z0-9]{1,12}$")
                 .match(relative)
                 .hasMatch())
            return failure("Invalid backup object path");
        if (files.contains(relative))
            return failure("Duplicate backup path");
        const auto path = ResourceRepository::safePath(source, relative);
        if (path.isEmpty() || !QFileInfo(path).isFile() ||
            QFileInfo(path).size() != item["size"].toInteger(-1))
            return failure("Backup file is missing or has changed: " + relative);
        auto hash = LibraryFileIO::hashFile(path, &cancel);
        if (!hash)
            return hash.error();
        if (hash.value() != item["sha256"].toString())
            return failure("Backup checksum mismatch: " + relative);
        files[relative] = item;
        bytes += QFileInfo(path).size();
        if (progress)
            progress(bytes, ++count);
    }
    if (!files.contains("library.sqlite"))
        return failure("Backup database is missing");
    Connection c(QDir(source).filePath("library.sqlite"));
    auto valid = validateDb(c.db);
    if (!valid)
        return valid.error();
    QSqlQuery q(c.db);
    QSet<QString> missing;
    for (const auto &v : m["missing"].toArray())
        missing.insert(v.toString());
    QSet<QString> expected{"library.sqlite"};
    if (!q.exec("SELECT relative_path,hash,size,state FROM audio_objects UNION ALL "
                "SELECT relative_path,hash,size,CASE WHEN state='ready' THEN 'ready' "
                "ELSE 'corrupt' END FROM artwork_objects"))
        return failure("Backup catalog schema is unsupported");
    while (q.next()) {
        const auto relative = q.value(0).toString(), hash = q.value(1).toString();
        if (q.value(3).toString() == "missing") {
            if (!missing.remove(hash))
                return failure("Missing-object manifest mismatch");
            continue;
        }
        if (q.value(3).toString() != "ready")
            return failure("Backup contains corrupt audio objects");
        expected.insert(relative);
        if (!files.contains(relative) || files[relative]["sha256"] != hash ||
            files[relative]["size"].toInteger() != q.value(2).toLongLong())
            return failure("Backup catalog and manifest disagree");
    }
    if (!missing.isEmpty() || expected.size() != files.size())
        return failure("Backup contains unregistered files");
    if (!q.exec("SELECT count(*) FROM songs s LEFT JOIN song_assets a ON "
                "a.song_id=s.id WHERE s.source_provider='' AND a.audio_hash IS "
                "NULL") ||
        !q.next() || q.value(0).toInt() != 0)
        return failure("Backup contains unmigrated external references");
    if (!q.exec("SELECT count(*) FROM songs") || !q.next() || q.value(0).toInt() != m["songs"].toInt(-1))
        return failure("Backup song count mismatch");
    return QJsonObject{{"source", QFileInfo(source).absoluteFilePath()},
                       {"version", m["version"]},
                       {"songs", m["songs"]},
                       {"bytes", bytes},
                       {"missing", m["missing"]},
                       {"manifest", m}};
}
Result<QJsonObject> prepareRestore(const QString &source, const QString &root, const QString &database,
                                   const std::atomic_bool &cancel, Progress progress) {
    auto valid = inspectBackup(source, cancel, progress);
    if (!valid)
        return valid.error();
    const auto id = token(), stage = QDir(root).filePath(".staging/restore-" + id);
    if (!QDir().mkpath(stage + "/media") || !QDir().mkpath(stage + "/artwork"))
        return failure("Cannot create restore staging directory", ErrorCode::Io);
    const auto dbNew = database + ".restore-new-" + id;
    QJsonArray actions;
    for (const auto &item : {"media", "artwork", "database"}) {
        const bool db = QString(item) == "database";
        const auto target = db ? database : QDir(root).filePath(item);
        actions.append(QJsonObject{{"target", target},
                                   {"new", db ? dbNew : stage + '/' + item},
                                   {"old", db ? database + ".restore-old-" + id
                                              : QDir(root).filePath(".rollback/" + id + '/' + item)},
                                   {"had_old", QFileInfo::exists(target)},
                                   {"state", "pending"}});
    }
    QJsonObject journal{
        {"id", id}, {"phase", "prepared"}, {"actions", actions}, {"songs", valid.value()["songs"]}};
    auto checked = validateJournal(journal, root, database);
    if (!checked)
        return checked.error();
    auto journalSaved = LibraryFileIO::writeJson(journalPath(root), journal);
    if (!journalSaved)
        return journalSaved.error();
    bool ok = false;
    struct Cleanup {
        QString stage, db, journal;
        bool &ok;
        ~Cleanup() {
            if (!ok) {
                QDir(stage).removeRecursively();
                QFile::remove(db);
                QFile::remove(journal);
            }
        }
    } cleanup{stage, dbNew, journalPath(root), ok};
    qint64 bytes = 0;
    int count = 0;
    for (const auto &v : valid.value()["manifest"].toObject()["files"].toArray()) {
        const auto f = v.toObject();
        const auto relative = f["path"].toString();
        const auto target =
            relative == "library.sqlite" ? dbNew : ResourceRepository::safePath(stage, relative);
        auto copied = LibraryFileIO::copyFile(ResourceRepository::safePath(source, relative), target, &cancel,
                                              [&](qint64 n) {
                                                  bytes += n;
                                                  if (progress)
                                                      progress(bytes, count);
                                              });
        if (!copied)
            return copied.error();
        if (copied.value() != f["sha256"].toString())
            return failure("Backup changed during restore");
        ++count;
    }
    ok = true;
    return journal;
}
Result<void> rollbackRestore(const QJsonObject &journal, const QString &root, const QString &database) {
    auto valid = validateJournal(journal, root, database);
    if (!valid)
        return valid;
    auto current = journal;
    if (QFileInfo::exists(journalPath(root))) {
        auto saved = LibraryFileIO::readJson(journalPath(root));
        if (!saved)
            return saved.error();
        if (saved.value()["id"] != journal["id"])
            return failure("Restore journal changed");
        auto checked = validateJournal(saved.value(), root, database);
        if (!checked)
            return checked;
        current = saved.value();
    }
    current["phase"] = "rolling_back";
    auto saved = LibraryFileIO::writeJson(journalPath(root), current);
    if (!saved)
        return saved;
    auto actions = current["actions"].toArray();
    for (qsizetype index = actions.size(); index > 0; --index) {
        const auto a = actions.at(index - 1).toObject();
        const auto target = a["target"].toString(), old = a["old"].toString();
        if (QFileInfo::exists(old)) {
            if (!removeEntry(target) || !QDir().rename(old, target))
                return failure("Cannot roll back restored library", ErrorCode::Io);
        } else if (!a["had_old"].toBool() && a["state"] != "pending" && a["state"] != "moving_old") {
            if (!removeEntry(target))
                return failure("Cannot remove incomplete restored object", ErrorCode::Io);
        }
        const auto next = a["new"].toString();
        if (!removeEntry(next))
            return failure("Cannot remove restored staging data", ErrorCode::Io);
        const QFileInfo temporary(next);
        for (const auto &name : temporary.dir().entryList({temporary.fileName() + ".copy-*"}, QDir::Files))
            if (!QFile::remove(temporary.dir().filePath(name)))
                return failure("Cannot remove partial restore copy", ErrorCode::Io);
    }
    const auto rollback = QDir(root).filePath(".rollback/" + current["id"].toString());
    QFile::remove(rollback + "/rollback.json");
    QDir().rmdir(rollback);
    QDir(QDir(root).filePath(".staging/restore-" + current["id"].toString())).removeRecursively();
    QFile::remove(journalPath(root));
    return {};
}
Result<void> applyRestore(QJsonObject journal, const QString &root, const QString &database) {
    auto valid = validateJournal(journal, root, database);
    if (!valid)
        return valid;
    for (const auto &suffix : {"-wal", "-journal"})
        if (QFileInfo(database + suffix).size() > 0)
            return failure("Close other database clients before restoring", ErrorCode::Unavailable);
    journal["phase"] = "switching";
    auto actions = journal["actions"].toArray();
    for (int i = 0; i < actions.size(); ++i) {
        auto a = actions[i].toObject();
        const auto target = a["target"].toString(), old = a["old"].toString();
        // Imports finishing during preparation may have created a directory.
        a["had_old"] = QFileInfo::exists(target);
        a["state"] = "moving_old";
        actions[i] = a;
        journal["actions"] = actions;
        auto saved = LibraryFileIO::writeJson(journalPath(root), journal);
        if (!saved)
            return saved;
        if (!QDir().mkpath(QFileInfo(old).absolutePath()))
            return failure("Cannot create rollback directory", ErrorCode::Io);
        if (QFileInfo::exists(target) && !QDir().rename(target, old))
            return failure("Cannot retain current library", ErrorCode::Io);
        a["state"] = "moving_new";
        actions[i] = a;
        journal["actions"] = actions;
        saved = LibraryFileIO::writeJson(journalPath(root), journal);
        if (!saved)
            return saved;
        if (!QDir().rename(a["new"].toString(), target))
            return failure("Cannot install restored library", ErrorCode::Io);
        a["state"] = "done";
        actions[i] = a;
        journal["actions"] = actions;
    }
    journal["phase"] = "installed";
    auto saved = LibraryFileIO::writeJson(journalPath(root), journal);
    if (!saved)
        return saved;
    return LibraryFileIO::writeJson(
        QDir(root).filePath(".rollback/" + journal["id"].toString() + "/rollback.json"), journal);
}
Result<void> recoverRestore(const QString &root, const QString &database) {
    if (!QFileInfo::exists(journalPath(root)))
        return {};
    auto journal = LibraryFileIO::readJson(journalPath(root));
    if (!journal)
        return journal.error();
    auto valid = validateJournal(journal.value(), root, database);
    if (!valid)
        return valid;
    if (journal.value()["phase"] == "installed") {
        Connection c(database);
        auto check = validateDb(c.db);
        if (check) {
            auto saved = LibraryFileIO::writeJson(
                QDir(root).filePath(".rollback/" + journal.value()["id"].toString() + "/rollback.json"),
                journal.value());
            if (!saved)
                return saved;
            QDir(QDir(root).filePath(".staging/restore-" + journal.value()["id"].toString()))
                .removeRecursively();
            QFile::remove(journalPath(root));
            return {};
        }
    }
    return rollbackRestore(journal.value(), root, database);
}
QStringList rollbackCopies(const QString &root) {
    QStringList ids;
    const QDir dir(QDir(root).filePath(".rollback"));
    for (const auto &id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (QFileInfo::exists(dir.filePath(id + "/rollback.json")))
            ids << id;
    return ids;
}
Result<void> removeRollback(const QString &root, const QString &database, const QString &id) {
    if (!QRegularExpression("^[a-f0-9-]{36}$").match(id).hasMatch())
        return failure("Invalid rollback ID");
    if (ResourceRepository::safePath(root, ".rollback/" + id + "/rollback.json").isEmpty())
        return failure("Unsafe rollback path");
    auto journal = LibraryFileIO::readJson(QDir(root).filePath(".rollback/" + id + "/rollback.json"));
    if (!journal)
        return journal.error();
    if (journal.value()["id"] != id)
        return failure("Rollback ID mismatch");
    auto valid = validateJournal(journal.value(), root, database);
    if (!valid)
        return valid;
    for (const auto &v : journal.value()["actions"].toArray())
        if (!removeEntry(v.toObject()["old"].toString()))
            return failure("Cannot remove rollback copy", ErrorCode::Io);
    QDir(QDir(root).filePath(".rollback/" + id)).removeRecursively();
    return {};
}
} // namespace nekotune::LibraryArchive
