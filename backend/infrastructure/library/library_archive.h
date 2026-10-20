#pragma once
#include "domain/lyrics/lyrics_types.h"
#include "infrastructure/library/library_file_io.h"
#include <QVector>
namespace nekotune::LibraryArchive {
struct ExportSong {
    int id = 0;
    QString title, artist, sourceName, audio, cover;
    qint64 duration = 0;
    std::optional<LyricsDocument> lyrics;
    QByteArray rawKrc;
};
using Progress = std::function<void(qint64, int)>;
Result<QJsonObject> exportSongs(const QVector<ExportSong> &songs, const QString &destination, bool playlist,
                                const std::atomic_bool &cancel, Progress progress = {});
Result<QJsonObject> backup(const QString &database, const QString &root, const QString &destination,
                           const std::atomic_bool &cancel, Progress progress = {});
Result<QJsonObject> inspectBackup(const QString &source, const std::atomic_bool &cancel,
                                  Progress progress = {});
Result<QJsonObject> prepareRestore(const QString &source, const QString &root, const QString &database,
                                   const std::atomic_bool &cancel, Progress progress = {});
Result<void> applyRestore(QJsonObject journal, const QString &root, const QString &database);
Result<void> recoverRestore(const QString &root, const QString &database);
Result<void> rollbackRestore(const QJsonObject &journal, const QString &root, const QString &database);
Result<void> snapshotDatabase(const QString &source, const QString &target);
QStringList rollbackCopies(const QString &root);
Result<void> removeRollback(const QString &root, const QString &database, const QString &id);
} // namespace nekotune::LibraryArchive
