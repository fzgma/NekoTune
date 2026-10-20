#pragma once
#include "application/command_scheduler.h"
#include "application/library/library_service.h"
#include "infrastructure/library/import_executor.h"
#include "infrastructure/library/music_directory.h"
#include <QObject>
#include <QSet>

namespace nekotune {
/// Verifies changed registered resources and recovers missing audio durations.
/// One file is inspected at a time to limit work and callback state.
/// Inspection happens off-thread; repository updates return to this thread.
/// Files are never implicitly imported by scanning the library directory.
/// finished aggregates counts/errors instead of aborting on one bad file.
class LibraryScanner final : public QObject {
    Q_OBJECT
  public:
    LibraryScanner(ImportExecutor &imports, MusicDirectory &music, LibraryService &library,
                   CommandScheduler &commands);
    /// Admit one scan at a time; return false while active or shutting down.
    /// A true return means discovery was admitted, not that every file was imported.
    /// Observe finished for aggregate imports, skips and per-file errors.
    /// The scan does not select a song or rewrite the playback queue.
    bool start();
    bool verifyAll() {
        if (m_active)
            return false;
        m_full = true;
        return start();
    }
    void shutdown() { m_stopping = true; }
  signals:
    void finished(int imported, int skipped, const QStringList &errors);

  private:
    void next();
    void backfillNext();
    ImportExecutor &m_imports;
    MusicDirectory &m_music;
    LibraryService &m_library;
    CommandScheduler &m_commands;
    QStringList m_paths, m_errors;
    QHash<QString, QString> m_expected;
    QSet<QString> m_covers;
    bool m_full = false;
    QVector<LibrarySong> m_missingDurations;
    QSet<QString> m_inspectedHashes;
    qsizetype m_backfillIndex = 0;
    int m_index = 0, m_imported = 0, m_skipped = 0;
    bool m_active = false, m_stopping = false;
};
} // namespace nekotune
