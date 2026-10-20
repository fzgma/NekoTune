#pragma once
#include "application/library/library_service.h"
#include "application/playback/player_engine.h"
#include "infrastructure/library/import_executor.h"
#include "infrastructure/library/library_archive.h"
#include "infrastructure/library/music_directory.h"
#include "ipc/ipc_router.h"
#include "storage/playlist_repository.h"
#include "storage/song_repository.h"
#include <QDateTime>
#include <QJsonArray>
#include <QPointer>
#include <QThread>
namespace nekotune {
class LibraryMaintenance final : public QObject {
    Q_OBJECT
  public:
    LibraryMaintenance(DatabaseSession &, MusicDirectory &, ImportExecutor &, SongRepository &,
                       PlaylistRepository &, LibraryService &, PlayerEngine &, IpcRouter &);
    ~LibraryMaintenance() override;
    void install();
    void startAutomatic();
    void retryCleanup();
    void shutdown();
    void restored(const QString &task, bool success, const QString &message = {});
    QJsonObject status() const;
  signals:
    void event(const QJsonObject &data);
    void restoreReady(const QJsonObject &journal, const QString &task);

  private:
    using Work = std::function<Result<QJsonObject>(const std::atomic_bool &, LibraryArchive::Progress)>;
    Result<QString> begin(const QString &kind);
    void work(Work fn, std::function<void(Result<QJsonObject>)> complete = {});
    void update(const QString &phase, qint64 bytes = -1, int files = -1);
    void finish(Result<QJsonObject> result);
    bool persist();
    void migrateNext();
    void migrationResult(const QString &error = {});
    Result<void> cleanLegacy(int songId, const QString &hash);
    void recoverImports(std::function<void()> complete = {});
    QVector<LibraryArchive::ExportSong> exportRows(const QVector<int> &ids);
    QJsonObject task(const QString &id) const;
    DatabaseSession &m_db;
    MusicDirectory &m_music;
    ImportExecutor &m_imports;
    SongRepository &m_songs;
    PlaylistRepository &m_playlists;
    LibraryService &m_library;
    PlayerEngine &m_player;
    IpcRouter &m_router;
    QThread m_thread;
    QObject *m_worker;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QString m_active;
    QJsonObject m_task;
    QVector<SongMetadata> m_migrations;
    int m_index = 0;
    QJsonArray m_errors;
    bool m_stopping = false;
    QDateTime m_startedAt = QDateTime::currentDateTimeUtc();
};
} // namespace nekotune
