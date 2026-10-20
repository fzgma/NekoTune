#pragma once
#include "domain/library/library_types.h"
#include "domain/library/managed_files.h"
#include "domain/result.h"
#include "storage/database_session.h"
namespace nekotune {
// Owns immutable content objects. SQL and publication run on the database
// thread; production imports provide an already copied and hashed worker
// result.
class MusicDirectory final : public IManagedFiles {
  public:
    explicit MusicDirectory(DatabaseSession &database, const QString &directory = {});
    Result<ImportedFile> manage(const ImportedFile &file);
    Result<void> publishArtwork(ImportedFile &file);
    // Providers receive a fresh staging basename, never a content-object write
    // path.
    Result<QString> reserveDownload(const QString &providerHash, const QString &title);
    Result<bool> ignored(const QString &hash) const;
    bool containsSong(const QString &hash) const;
    // Read-only compatibility lookup for not-yet-migrated numbered resources.
    QString baseFor(const QString &hash) const;
    QString directory() const { return m_directory; }
    DatabaseSession &database() const { return m_database; }
    void setPinned(bool pinned) { m_pinned = pinned; }
    bool pinned() const { return m_pinned; }
    // Called after associations are removed inside the caller's transaction.
    // Explicit retain markers and remaining references both prevent collection.
    Result<std::unique_ptr<IManagedFileRemoval>> stageRemoval(const QStringList &keys) override;

  private:
    DatabaseSession &m_database;
    QString m_directory;
    bool m_pinned = false;
};
} // namespace nekotune
