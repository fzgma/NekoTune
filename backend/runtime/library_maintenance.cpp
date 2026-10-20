#include "runtime/library_maintenance.h"
#include "application/transaction.h"
#include "domain/lyrics/lrc_parser.h"
#include "infrastructure/lyrics/lyrics_cache.h"
#include "ipc/api/api_context.h"
#include "ipc/serialization/serialization.h"
#include "storage/resource_repository.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>
namespace nekotune {
LibraryMaintenance::LibraryMaintenance(DatabaseSession &db, MusicDirectory &music, ImportExecutor &imports,
                                       SongRepository &songs, PlaylistRepository &playlists,
                                       LibraryService &library, PlayerEngine &player, IpcRouter &router)
    : m_db(db), m_music(music), m_imports(imports), m_songs(songs), m_playlists(playlists),
      m_library(library), m_player(player), m_router(router), m_worker(new QObject),
      m_cancel(std::make_shared<std::atomic_bool>(false)) {
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QSqlQuery q(m_db.database());
    if (q.exec("SELECT id,data FROM storage_tasks WHERE state IN "
               "('running','switching')")) {
        QVector<QPair<QString, QJsonObject>> interrupted;
        while (q.next()) {
            auto data = QJsonDocument::fromJson(q.value(1).toByteArray()).object();
            data["state"] = "interrupted";
            data["message"] = "Task interrupted by shutdown; migration can be retried";
            interrupted.append({q.value(0).toString(), data});
        }
        q.finish();
        for (const auto &[id, data] : interrupted) {
            q.prepare("UPDATE storage_tasks SET state='interrupted',data=? WHERE id=?");
            q.addBindValue(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
            q.addBindValue(id);
            q.exec();
        }
    }
}
LibraryMaintenance::~LibraryMaintenance() { shutdown(); }
void LibraryMaintenance::shutdown() {
    if (m_stopping)
        return;
    m_stopping = true;
    m_cancel->store(true);
    m_thread.quit();
    m_thread.wait();
}
Result<QString> LibraryMaintenance::begin(const QString &kind) {
    if (m_stopping || !m_active.isEmpty())
        return failure("A library task is already running", ErrorCode::Unavailable);
    m_active = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_task = {
        {"task", m_active},    {"kind", kind},
        {"state", "running"},  {"phase", "preparing"},
        {"cancellable", true}, {"files", 0},
        {"bytes", 0},          {"started_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (!persist()) {
        m_active.clear();
        return failure("Cannot persist library task", ErrorCode::Storage);
    }
    emit event(QJsonObject{{"event", "library.task"}, {"data", m_task}});
    return m_active;
}
bool LibraryMaintenance::persist() {
    QSqlQuery q(m_db.database());
    q.prepare("INSERT INTO storage_tasks(id,kind,state,data) VALUES(?,?,?,?) ON "
              "CONFLICT(id) DO UPDATE SET state=excluded.state,data=excluded.data");
    q.addBindValue(m_task["task"].toString());
    q.addBindValue(m_task["kind"].toString());
    q.addBindValue(m_task["state"].toString());
    q.addBindValue(QString::fromUtf8(QJsonDocument(m_task).toJson(QJsonDocument::Compact)));
    return q.exec();
}
void LibraryMaintenance::update(const QString &phase, qint64 bytes, int files) {
    if (m_active.isEmpty() || m_stopping)
        return;
    m_task["phase"] = phase;
    if (bytes >= 0)
        m_task["bytes"] = bytes;
    if (files >= 0)
        m_task["files"] = files;
    persist();
    emit event({{"event", "library.task"}, {"data", m_task}});
}
void LibraryMaintenance::finish(Result<QJsonObject> result) {
    if (m_stopping)
        return;
    m_task["state"] = result ? "finished" : m_cancel->load() ? "cancelled" : "failed";
    if (result)
        m_task["result"] = result.value();
    else
        m_task["message"] = result.error().message;
    if (!persist()) {
        m_task["state"] = "failed";
        m_task["message"] = "Cannot persist library task result";
    }
    m_active.clear();
    m_music.setPinned(false);
    m_router.setMaintenance(false);
    emit event({{"event", "library.task"}, {"data", m_task}});
    emit event({{"event", "library.storage_changed"}});
    emit m_library.changed();
}
void LibraryMaintenance::work(Work fn, std::function<void(Result<QJsonObject>)> complete) {
    QPointer<LibraryMaintenance> guard(this);
    const auto cancel = m_cancel;
    const auto id = m_active;
    QMetaObject::invokeMethod(
        m_worker, [guard, cancel, id, fn = std::move(fn), complete = std::move(complete)]() mutable {
            auto last = std::make_shared<qint64>(0);
            auto progress = [guard, id, last](qint64 bytes, int files) {
                const auto now = QDateTime::currentMSecsSinceEpoch();
                if (now - *last < 150)
                    return;
                *last = now;
                if (guard)
                    QMetaObject::invokeMethod(guard, [guard, id, bytes, files] {
                        if (guard && guard->m_active == id)
                            guard->update(guard->m_task["phase"].toString(), bytes, files);
                    });
            };
            auto result = fn(*cancel, progress);
            if (guard)
                QMetaObject::invokeMethod(
                    guard, [guard, id, result = std::move(result), complete = std::move(complete)]() mutable {
                        if (!guard || guard->m_stopping || guard->m_active != id)
                            return;
                        if (complete)
                            complete(std::move(result));
                        else
                            guard->finish(std::move(result));
                    });
        });
}
QJsonObject LibraryMaintenance::task(const QString &id) const {
    QSqlQuery q(m_db.database());
    q.prepare("SELECT data FROM storage_tasks WHERE id=?");
    q.addBindValue(id);
    return q.exec() && q.next() ? QJsonDocument::fromJson(q.value(0).toByteArray()).object() : QJsonObject();
}
QJsonObject LibraryMaintenance::status() const {
    auto result = ResourceRepository(m_db).status();
    result["active_task"] = m_active;
    result["rollback_copies"] =
        QJsonArray::fromStringList(LibraryArchive::rollbackCopies(m_music.directory()));
    QJsonArray tasks;
    QSqlQuery q(m_db.database());
    if (q.exec("SELECT data FROM storage_tasks ORDER BY rowid DESC LIMIT 20"))
        while (q.next())
            tasks.append(QJsonDocument::fromJson(q.value(0).toByteArray()).object());
    result["tasks"] = tasks;
    QJsonArray errors;
    if (q.exec("SELECT song_id,state,error FROM storage_migrations WHERE "
               "state<>'done'"))
        while (q.next())
            errors.append(QJsonObject{{"song_id", q.value(0).toInt()},
                                      {"state", q.value(1).toString()},
                                      {"message", q.value(2).toString()}});
    result["migration_errors"] = errors;
    return result;
}
QVector<LibraryArchive::ExportSong> LibraryMaintenance::exportRows(const QVector<int> &ids) {
    QVector<LibraryArchive::ExportSong> rows;
    ResourceRepository assets(m_db);
    for (int id : ids) {
        auto song = m_songs.songById(id);
        if (!song)
            continue;
        auto lyrics = assets.lyrics(id, "local");
        QString kind = "local";
        if (!lyrics && !song->lyrics.trimmed().isEmpty()) {
            LyricsDocument d;
            d.source = "custom";
            if (LrcParser::looksLikeLrc(song->lyrics))
                d.syncedLyrics = song->lyrics;
            else
                d.plainLyrics = song->lyrics;
            lyrics = d;
            kind = "custom";
        }
        if (!lyrics) {
            lyrics = assets.lyrics(id, "selected");
            kind = "selected";
        }
        rows.append({id, song->customTitle, song->artist, song->sourceName, m_library.availablePath(id),
                     assets.coverPath(id), song->durationMs, lyrics, assets.rawKrc(id, kind)});
    }
    return rows;
}
void LibraryMaintenance::install() {
    m_router.registerMethod("library.storage_status",
                            [this](const auto &, auto done) { done(success(status())); });
    m_router.registerMethod("library.task", [this](const auto &p, auto done) {
        auto data = task(p.value("task").toString());
        done(data.isEmpty() ? error(failure("Task not found")) : success(data));
    });
    m_router.registerMethod("library.cancel", [this](const auto &p, auto done) {
        if (m_active.isEmpty() || p.value("task") != m_active || m_task["state"] == "switching" ||
            m_task["cancellable"] == false)
            done(error(failure("Task cannot be cancelled")));
        else {
            m_cancel->store(true);
            done(success());
        }
    });
    m_router.registerMethod(
        "library.migrate",
        [this](const auto &, auto done) {
            auto started = begin("migrate");
            done(started ? success({{"task", started.value()}}) : error(started.error()));
            if (started)
                QTimer::singleShot(0, this, &LibraryMaintenance::startAutomatic);
        },
        true);
    m_router.registerMethod(
        "library.export",
        [this](const QJsonObject &p, auto done) {
            QVector<int> ids;
            bool playlist = p.contains("playlist_id");
            if (playlist) {
                auto selected = m_playlists.playlistById(p["playlist_id"].toInt());
                if (!selected) {
                    done(error(failure("Playlist not found")));
                    return;
                }
                for (const auto &item : selected->items)
                    ids << item.songId;
            } else {
                if (!p["song_ids"].isArray()) {
                    done(error(failure("song_ids required")));
                    return;
                }
                for (const auto &value : p["song_ids"].toArray()) {
                    if (!positiveId(value) || !m_songs.songById(value.toInt())) {
                        done(error(failure("Invalid song ID")));
                        return;
                    }
                    ids << value.toInt();
                }
            }
            const auto destination = p["directory"].toString();
            if (ids.isEmpty() || destination.isEmpty() ||
                LibraryFileIO::within(m_music.directory(), destination)) {
                done(error(failure("Select songs and an export directory outside the library")));
                return;
            }
            auto started = begin("export");
            if (!started) {
                done(error(started.error()));
                return;
            }
            auto rows = exportRows(ids);
            m_music.setPinned(true);
            done(success({{"task", started.value()}}));
            update("exporting");
            work([rows, destination, playlist](const auto &cancel, auto progress) {
                return LibraryArchive::exportSongs(rows, destination, playlist, cancel, progress);
            });
        },
        true);
    m_router.registerMethod(
        "library.verify",
        [this](const QJsonObject &, auto done) {
            auto started = begin("verify");
            if (!started) {
                done(error(started.error()));
                return;
            }
            QJsonArray objects;
            QSqlQuery q(m_db.database());
            if (!q.exec("SELECT relative_path,hash,'audio' FROM audio_objects UNION "
                        "ALL SELECT relative_path,hash,'cover' FROM artwork_objects")) {
                finish(failure(q.lastError().text(), ErrorCode::Storage));
                done(error(failure(q.lastError().text())));
                return;
            }
            while (q.next())
                objects.append(QJsonObject{{"path", q.value(0).toString()},
                                           {"hash", q.value(1).toString()},
                                           {"kind", q.value(2).toString()}});
            m_task["total_files"] = objects.size();
            m_music.setPinned(true);
            done(success({{"task", started.value()}}));
            update("verifying");
            work(
                [root = m_music.directory(), objects](const auto &cancel,
                                                      auto progress) -> Result<QJsonObject> {
                    QJsonArray results, errors;
                    qint64 bytes = 0;
                    int count = 0;
                    for (const auto &v : objects) {
                        if (cancel.load())
                            return failure("Cancelled", ErrorCode::Cancelled);
                        auto item = v.toObject();
                        const auto path = ResourceRepository::safePath(root, item["path"].toString());
                        auto digest = path.isEmpty() ? Result<QString>(failure("Unsafe object path"))
                                                     : LibraryFileIO::hashFile(path, &cancel);
                        const bool valid = digest && digest.value() == item["hash"].toString();
                        item["state"] = valid ? "ready" : QFileInfo::exists(path) ? "corrupt" : "missing";
                        item["size"] = QFileInfo(path).size();
                        item["mtime"] = QFileInfo(path).lastModified().toMSecsSinceEpoch();
                        if (!valid)
                            errors.append(QJsonObject{
                                {"path", item["path"]},
                                {"message", digest ? "Content checksum mismatch" : digest.error().message}});
                        results.append(item);
                        bytes += QFileInfo(path).size();
                        if (progress)
                            progress(bytes, ++count);
                    }
                    return QJsonObject{{"objects", results},
                                       {"errors", errors},
                                       {"failed", errors.size()},
                                       {"verified", count}};
                },
                [this](Result<QJsonObject> result) {
                    if (!result) {
                        finish(result.error());
                        return;
                    }
                    QString databaseError;
                    {
                        Transaction tx(m_db);
                        if (!tx)
                            databaseError = m_db.errorString();
                        else {
                            for (const auto &v : result.value()["objects"].toArray()) {
                                const auto item = v.toObject();
                                QSqlQuery q(m_db.database());
                                const auto table = item["kind"] == "audio" ? QString("audio_objects")
                                                                           : QString("artwork_objects");
                                q.prepare("UPDATE " + table +
                                          " SET state=?,mtime=? WHERE "
                                          "hash=?");
                                q.addBindValue(item["state"].toString());
                                q.addBindValue(item["mtime"].toInteger());
                                q.addBindValue(item["hash"].toString());
                                if (!q.exec()) {
                                    databaseError = q.lastError().text();
                                    break;
                                }
                            }
                            if (databaseError.isEmpty() && !tx.commit())
                                databaseError = m_db.errorString();
                        }
                    }
                    if (!databaseError.isEmpty()) {
                        finish(failure(databaseError, ErrorCode::Storage));
                        return;
                    }
                    result.value().remove("objects");
                    finish(result);
                });
        },
        true);
    m_router.registerMethod(
        "library.backup",
        [this](const QJsonObject &p, auto done) {
            const auto destination = p["directory"].toString();
            if (destination.isEmpty()) {
                done(error(failure("Backup directory required")));
                return;
            }
            if (ResourceRepository(m_db).status()["pending_migration"].toInt() > 0) {
                done(error(failure("Finish migrating external references before backing up")));
                return;
            }
            auto started = begin("backup");
            if (!started) {
                done(error(started.error()));
                return;
            }
            m_music.setPinned(true);
            done(success({{"task", started.value()}}));
            update("backing_up");
            work([db = m_db.databasePath(), root = m_music.directory(), destination](const auto &cancel,
                                                                                     auto progress) {
                return LibraryArchive::backup(db, root, destination, cancel, progress);
            });
        },
        true);
    m_router.registerMethod("library.restore_preview", [this](const QJsonObject &p, auto done) {
        const auto source = p["directory"].toString();
        if (source.isEmpty()) {
            done(error(failure("Backup directory required")));
            return;
        }
        auto started = begin("restore_preview");
        if (!started) {
            done(error(started.error()));
            return;
        }
        done(success({{"task", started.value()}}));
        update("validating");
        work([source](const auto &cancel, auto progress) {
            auto result = LibraryArchive::inspectBackup(source, cancel, progress);
            if (result)
                result.value().remove("manifest");
            return result;
        });
    });
    m_router.registerMethod(
        "library.restore",
        [this](const QJsonObject &p, auto done) {
            const auto preview = task(p["preview_task"].toString());
            if (p["confirmed"] != true || preview["kind"] != "restore_preview" ||
                preview["state"] != "finished") {
                done(error(failure("A successful preview and confirmation are required")));
                return;
            }
            const auto source = preview["result"].toObject()["source"].toString();
            auto started = begin("restore");
            if (!started) {
                done(error(started.error()));
                return;
            }
            m_music.setPinned(true);
            m_router.setMaintenance(true);
            done(success({{"task", started.value()}}));
            update("preparing_restore");
            work(
                [source, root = m_music.directory(), db = m_db.databasePath()](const auto &cancel,
                                                                               auto progress) {
                    return LibraryArchive::prepareRestore(source, root, db, cancel, progress);
                },
                [this](Result<QJsonObject> result) {
                    if (!result) {
                        finish(result.error());
                        return;
                    }
                    if (m_cancel->load()) {
                        LibraryArchive::rollbackRestore(result.value(), m_music.directory(),
                                                        m_db.databasePath());
                        finish(failure("Cancelled", ErrorCode::Cancelled));
                        return;
                    }
                    m_task["state"] = "switching";
                    update("switching");
                    emit restoreReady(result.value(), m_active);
                });
        },
        true);
    m_router.registerMethod(
        "library.remove_rollback",
        [this](const QJsonObject &p, auto done) {
            if (!m_active.isEmpty() || p["confirmed"] != true ||
                !QRegularExpression("^[a-f0-9-]{36}$").match(p["id"].toString()).hasMatch()) {
                done(error(failure("Confirm cleanup while no library task is running")));
                return;
            }
            auto started = begin("remove_rollback");
            if (!started) {
                done(error(started.error()));
                return;
            }
            m_task["cancellable"] = false;
            update("cleaning_rollback");
            done(success({{"task", started.value()}}));
            work([root = m_music.directory(), db = m_db.databasePath(),
                  id = p["id"].toString()](const auto &cancel, auto) -> Result<QJsonObject> {
                if (cancel.load())
                    return failure("Cancelled", ErrorCode::Cancelled);
                auto result = LibraryArchive::removeRollback(root, db, id);
                return result ? Result<QJsonObject>(QJsonObject{{"removed", id}})
                              : Result<QJsonObject>(result.error());
            });
        },
        true);
}
void LibraryMaintenance::startAutomatic() {
    if (m_stopping)
        return;
    const bool automatic = m_active.isEmpty();
    if (!automatic && m_task["kind"] != "migrate")
        return;
    m_migrations.clear();
    m_index = 0;
    m_errors = {};
    ResourceRepository resources(m_db);
    for (const auto &song : m_songs.songs()) {
        QSqlQuery q(m_db.database());
        q.prepare("SELECT state FROM storage_migrations WHERE song_id=?");
        q.addBindValue(song.id);
        q.exec();
        const bool unfinished = q.next() && q.value(0).toString() != "done";
        if ((song.providerId.isEmpty() && !resources.hasAudio(song.id)) || unfinished)
            m_migrations << song;
    }
    if (automatic && m_migrations.isEmpty()) {
        recoverImports();
        return;
    }
    if (automatic) {
        auto started = begin("migrate");
        if (!started)
            return;
    }
    m_task["total_files"] = m_migrations.size();
    update("migrating");
    const auto snapshot = QDir(m_music.directory()).filePath("config/pre-library-migration.sqlite3");
    if (!m_migrations.isEmpty() && !QFileInfo::exists(snapshot)) {
        work(
            [db = m_db.databasePath(), snapshot](const auto &cancel, auto) -> Result<QJsonObject> {
                if (cancel.load())
                    return failure("Cancelled", ErrorCode::Cancelled);
                auto result = LibraryArchive::snapshotDatabase(db, snapshot);
                return result ? Result<QJsonObject>(QJsonObject{}) : Result<QJsonObject>(result.error());
            },
            [this](auto result) {
                if (!result)
                    finish(result.error());
                else {
                    recoverImports([this] { migrateNext(); });
                }
            });
    } else {
        recoverImports([this] { migrateNext(); });
    }
}
void LibraryMaintenance::recoverImports(std::function<void()> complete) {
    const QDir staging(QDir(m_music.directory()).filePath(".staging"));
    QJsonArray interrupted;
    for (const auto &name : staging.entryList({"import-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = ResourceRepository::safePath(m_music.directory(), ".staging/" + name);
        if (path.isEmpty())
            continue;
        auto journal = LibraryFileIO::readJson(QDir(path).filePath("import.json"));
        if (!journal)
            continue;
        const auto hash = journal.value()["hash"].toString(), stage = journal.value()["stage"].toString();
        QSqlQuery q(m_db.database());
        q.prepare("SELECT 1 FROM song_assets WHERE audio_hash=?");
        q.addBindValue(hash);
        if (q.exec() && q.next() && stage == "published") {
            QDir(path).removeRecursively();
            continue;
        }
        if (QFileInfo(QDir(path).filePath("import.json")).lastModified() >= m_startedAt)
            continue;
        if (stage == "copied") {
            QDir(path).removeRecursively();
            continue;
        }
        if (stage == "published" || stage == "publishing") {
            auto item = journal.value();
            item["directory"] = path;
            interrupted.append(item);
        }
    }
    if (interrupted.isEmpty()) {
        if (complete)
            complete();
        return;
    }
    const bool standalone = m_active.isEmpty();
    if (standalone) {
        auto started = begin("recovery");
        if (!started)
            return;
    }
    update("recovering");
    work(
        [root = m_music.directory(), interrupted](const auto &cancel, auto) -> Result<QJsonObject> {
            QJsonArray objects;
            for (const auto &v : interrupted) {
                auto item = v.toObject();
                const auto relative = item["relative_path"].toString(), hash = item["hash"].toString();
                if (!QRegularExpression("^media/[a-f0-9]{2}/[a-f0-9]{64}\\.[a-z0-9]{1,12}$")
                         .match(relative)
                         .hasMatch() ||
                    QFileInfo(relative).completeBaseName() != hash)
                    return failure("Invalid interrupted import journal");
                const auto path = ResourceRepository::safePath(root, relative);
                if (path.isEmpty())
                    return failure("Unsafe interrupted import path");
                if (!QFileInfo::exists(path))
                    continue;
                auto digest = LibraryFileIO::hashFile(path, &cancel);
                if (!digest)
                    return digest.error();
                if (digest.value() != hash)
                    return failure("Interrupted audio publication failed checksum verification");
                item["size"] = QFileInfo(path).size();
                item["mtime"] = QFileInfo(path).lastModified().toMSecsSinceEpoch();
                objects.append(item);
            }
            return QJsonObject{{"objects", objects}};
        },
        [this, complete = std::move(complete), standalone](Result<QJsonObject> result) {
            if (!result) {
                finish(result.error());
                return;
            }
            const auto objects = result.value()["objects"].toArray();
            for (const auto &v : objects) {
                const auto item = v.toObject();
                QSqlQuery q(m_db.database());
                q.prepare("INSERT OR IGNORE INTO "
                          "audio_objects(hash,relative_path,size,mtime,state,"
                          "retained) VALUES(?,?,?,?,'ready',1)");
                q.addBindValue(item["hash"].toString());
                q.addBindValue(item["relative_path"].toString());
                q.addBindValue(item["size"].toInteger());
                q.addBindValue(item["mtime"].toInteger());
                if (!q.exec()) {
                    finish(failure(q.lastError().text(), ErrorCode::Storage));
                    return;
                }
                QDir(item["directory"].toString()).removeRecursively();
            }
            if (standalone)
                finish(QJsonObject{{"recovered", objects.size()}});
            else if (complete)
                complete();
        });
}
void LibraryMaintenance::migrateNext() {
    if (m_stopping)
        return;
    if (m_cancel->load()) {
        finish(failure("Cancelled", ErrorCode::Cancelled));
        return;
    }
    if (m_index >= m_migrations.size()) {
        finish(QJsonObject{{"processed", m_index}, {"failed", m_errors.size()}, {"errors", m_errors}});
        return;
    }
    const auto song = m_migrations[m_index];
    ResourceRepository resources(m_db);
    QSqlQuery old(m_db.database());
    old.prepare("SELECT state,source FROM storage_migrations WHERE song_id=?");
    old.addBindValue(song.id);
    old.exec();
    const bool remembered = old.next();
    if (remembered && old.value(0) == "cleanup_pending" && resources.hasAudio(song.id)) {
        const auto audio = resources.audioPath(song.id);
        work(
            [audio, hash = song.hash](const auto &cancel, auto) -> Result<QJsonObject> {
                auto verified = LibraryFileIO::hashFile(audio, &cancel);
                if (!verified)
                    return verified.error();
                if (verified.value() != hash)
                    return failure("Migrated audio changed; old files retained");
                return QJsonObject{};
            },
            [this, song](auto verified) {
                if (!verified) {
                    migrationResult(verified.error().message);
                    return;
                }
                auto cleaned = cleanLegacy(song.id, song.hash);
                migrationResult(cleaned ? QString() : cleaned.error().message);
            });
        return;
    }
    QString source = remembered ? old.value(1).toString() : song.firstPath;
    const auto base = m_music.baseFor(song.hash);
    if (!base.isEmpty() && QFileInfo::exists(base + ".audio.json"))
        source = base + ".audio.json";
    if (source.isEmpty() || !QFileInfo::exists(source))
        for (const auto &candidate : m_songs.pathsForSong(song.id))
            if (QFileInfo(candidate).isFile()) {
                source = candidate;
                break;
            }
    QSqlQuery q(m_db.database());
    q.prepare("INSERT INTO storage_migrations(song_id,state,source,error) "
              "VALUES(?,'copying',?,'') ON CONFLICT(song_id) DO UPDATE SET "
              "state='copying',source=excluded.source,error=''");
    q.addBindValue(song.id);
    q.addBindValue(source);
    if (!q.exec()) {
        migrationResult(q.lastError().text());
        return;
    }
    QPointer<LibraryMaintenance> guard(this);
    m_imports.inspect(source, [guard, song](Result<ImportedFile> file) {
        if (!guard || guard->m_stopping)
            return;
        guard->m_router.commands().submit([guard, song, file = std::move(file)](auto done) {
            if (!guard || guard->m_stopping) {
                done();
                return;
            }
            QString error;
            if (!file)
                error = file.error().message;
            else if (file.value().hash != song.hash)
                error = "Legacy source content does not match the saved song";
            else if (guard->m_cancel->load())
                error = "Cancelled";
            else {
                auto attached = guard->m_library.attachFile(song.id, file.value());
                if (!attached)
                    error = attached.error().message;
                else {
                    LyricsQuery query;
                    query.trackId = song.resourceKey();
                    const auto cached = LyricsCache().read(query);
                    if (cached && !ResourceRepository(guard->m_db).lyrics(song.id, "selected") &&
                        !ResourceRepository(guard->m_db).saveLyrics(song.id, *cached, "selected"))
                        error = "Cannot migrate selected lyrics";
                    QSqlQuery q(guard->m_db.database());
                    q.prepare("UPDATE storage_migrations SET "
                              "state='cleanup_pending',error='' WHERE song_id=?");
                    q.addBindValue(song.id);
                    if (error.isEmpty()) {
                        if (!q.exec())
                            error = q.lastError().text();
                        else {
                            auto cleaned = guard->cleanLegacy(song.id, song.hash);
                            if (!cleaned)
                                error = cleaned.error().message;
                        }
                    }
                }
            }
            done();
            guard->migrationResult(error);
        });
    });
}
Result<void> LibraryMaintenance::cleanLegacy(int id, const QString &hash) {
    const auto current = m_player.snapshot().song;
    if (current && current->metadata.id == id && !m_player.loadedSource().isEmpty())
        return failure("Cleanup deferred until the current song is released", ErrorCode::Unavailable);
    ResourceRepository resources(m_db);
    const auto audio = resources.audioPath(id);
    if (audio.isEmpty() || !QFileInfo(audio).isFile())
        return failure("Migrated audio is unavailable", ErrorCode::Io);
    const auto base = m_music.baseFor(hash);
    if (m_music.pinned())
        return failure("Cleanup deferred while library files are in use", ErrorCode::Unavailable);
    if (!base.isEmpty()) {
        const QFileInfo folder(QFileInfo(base).absolutePath());
        if (folder.isSymLink() || folder.dir().absolutePath() != QDir(m_music.directory()).absolutePath())
            return failure("Unsafe legacy directory");
        QSqlQuery q(m_db.database());
        q.prepare("SELECT path FROM managed_resources WHERE hash=?");
        q.addBindValue(hash);
        if (!q.exec())
            return failure(q.lastError().text(), ErrorCode::Storage);
        QStringList paths;
        if (q.next()) {
            const auto path = q.value(0).toString();
            if (QFileInfo(path).absolutePath() == folder.absoluteFilePath() &&
                QFileInfo(path).completeBaseName() == folder.fileName())
                paths << path;
        }
        paths << base + ".audio.json";
        for (const auto &suffix : {"krc", "lrc", "txt", "jpg", "jpeg", "png", "webp"})
            paths << base + '.' + suffix;
        for (const auto &path : paths)
            if ((QFileInfo::exists(path) || QFileInfo(path).isSymLink()) && !QFile::remove(path))
                return failure("Could not remove old managed file: " + path, ErrorCode::Io);
        QDir().rmdir(folder.absoluteFilePath());
    }
    QSqlQuery q(m_db.database());
    q.prepare("UPDATE storage_migrations SET state='done',error='' WHERE song_id=?");
    q.addBindValue(id);
    return q.exec() ? Result<void>() : Result<void>(failure(q.lastError().text(), ErrorCode::Storage));
}
void LibraryMaintenance::migrationResult(const QString &message) {
    if (!message.isEmpty()) {
        const auto song = m_migrations[m_index];
        m_errors.append(QJsonObject{{"song_id", song.id}, {"message", message}});
        QSqlQuery q(m_db.database());
        q.prepare("UPDATE storage_migrations SET state=CASE WHEN state='cleanup_pending' "
                  "THEN state ELSE 'failed' END,error=? WHERE song_id=?");
        q.addBindValue(message);
        q.addBindValue(song.id);
        q.exec();
    }
    ++m_index;
    update("migrating", -1, m_index);
    QTimer::singleShot(0, this, &LibraryMaintenance::migrateNext);
}
void LibraryMaintenance::retryCleanup() {
    if (m_stopping || !m_active.isEmpty())
        return;
    QSqlQuery q(m_db.database());
    if (q.exec("SELECT 1 FROM storage_migrations WHERE state='cleanup_pending' "
               "LIMIT 1") &&
        q.next())
        QTimer::singleShot(0, this, &LibraryMaintenance::startAutomatic);
}
void LibraryMaintenance::restored(const QString &id, bool ok, const QString &message) {
    m_active = id;
    m_task = {{"task", id}, {"kind", "restore"}, {"state", "running"}};
    finish(ok ? Result<QJsonObject>(QJsonObject{{"restored", true}}) : Result<QJsonObject>(failure(message)));
}
} // namespace nekotune
