#include "runtime/library_scanner.h"
#include "storage/resource_repository.h"
#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QSqlQuery>
#include <QTimer>
namespace nekotune {
LibraryScanner::LibraryScanner(ImportExecutor &imports, MusicDirectory &music, LibraryService &library,
                               CommandScheduler &commands)
    : m_imports(imports), m_music(music), m_library(library), m_commands(commands) {}
bool LibraryScanner::start() {
    if (m_active || m_stopping)
        return false;
    m_active = true;
    m_index = m_imported = m_skipped = 0;
    m_errors.clear();
    m_paths.clear();
    m_expected.clear();
    m_covers.clear();
    QSqlQuery q(m_music.database().database());
    if (q.exec("SELECT relative_path,hash,size,mtime,state,0 FROM audio_objects "
               "UNION ALL SELECT relative_path,hash,size,mtime,state,1 FROM "
               "artwork_objects"))
        while (q.next()) {
            const auto path = ResourceRepository::safePath(m_music.directory(), q.value(0).toString());
            if (path.isEmpty()) {
                m_errors << "Unsafe catalog path";
                continue;
            }
            const QFileInfo file(path);
            if (!m_full && file.isFile() && file.size() == q.value(2).toLongLong() &&
                file.lastModified().toMSecsSinceEpoch() == q.value(3).toLongLong() && q.value(4) == "ready") {
                ++m_skipped;
                continue;
            }
            m_paths << path;
            m_expected[path] = q.value(1).toString();
            if (q.value(5).toBool())
                m_covers.insert(path);
        }
    m_missingDurations.clear();
    for (const auto &song : m_library.snapshot().songs)
        if (song.metadata.durationMs <= 0 && !song.path.isEmpty()) {
            m_missingDurations.append(song);
            if (!m_expected.contains(song.path)) {
                m_paths << song.path;
                m_expected[song.path] = song.metadata.hash;
            }
        }
    QTimer::singleShot(0, this, &LibraryScanner::next);
    return true;
}
void LibraryScanner::next() {
    if (m_stopping)
        return;
    if (m_index >= m_paths.size()) {
        m_active = false;
        m_full = false;
        emit m_library.changed();
        emit finished(0, m_skipped, m_errors);
        return;
    }
    const auto path = m_paths[m_index++], hash = m_expected.value(path);
    QPointer<LibraryScanner> guard(this);
    auto completed = [guard, path, hash](Result<ImportedFile> file) {
        if (!guard || guard->m_stopping)
            return;
        guard->m_commands.submit([guard, path, hash, file = std::move(file)](auto done) {
            if (guard && !guard->m_stopping) {
                const bool valid = file && file.value().hash == hash;
                QSqlQuery q(guard->m_music.database().database());
                const auto table =
                    guard->m_covers.contains(path) ? QString("artwork_objects") : QString("audio_objects");
                q.prepare("UPDATE " + table +
                          " SET state=?,size=CASE WHEN ? THEN ? ELSE "
                          "size END,mtime=CASE WHEN ? THEN ? ELSE mtime END WHERE hash=?");
                q.addBindValue(valid ? "ready" : QFileInfo::exists(path) ? "corrupt" : "missing");
                q.addBindValue(valid);
                q.addBindValue(QFileInfo(path).size());
                q.addBindValue(valid);
                q.addBindValue(QFileInfo(path).lastModified().toMSecsSinceEpoch());
                q.addBindValue(hash);
                if (!q.exec() || !valid)
                    guard->m_errors << path + ": " +
                                           (file ? "Content verification failed" : file.error().message);
                else
                    ++guard->m_skipped;
                if (valid && !guard->m_covers.contains(path))
                    for (const auto &song : guard->m_missingDurations)
                        if (song.metadata.hash == hash)
                            guard->m_library.backfillDuration(song.metadata.id, hash,
                                                              file.value().durationMs);
                QTimer::singleShot(0, guard, &LibraryScanner::next);
            }
            done();
        });
    };
    if (m_covers.contains(path))
        m_imports.inspectObject(path, std::move(completed));
    else
        m_imports.inspectUnmanaged(path, std::move(completed));
}
void LibraryScanner::backfillNext() { next(); }
} // namespace nekotune
