#pragma once
#include "domain/result.h"
#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>

namespace nekotune::zip {
/// Limits applied while extracting; exceeding any of them rejects the whole archive.
struct Limits {
    qint64 archiveBytes = 0; // 0 = unlimited
    qint64 unpackedBytes = 512LL * 1024 * 1024;
    int files = 20'000;
};
/// Extract a classic (non-ZIP64, unencrypted) stored/deflated archive into destination.
/// Rejects absolute paths, "..", backslashes, links and special files. Executable entries get
/// owner-only rwx, others owner-only rw.
Result<void> extract(const QString &archive, const QString &destination, const Limits &limits = {});

/// Builds an archive in memory and writes it atomically.
class Writer {
  public:
    void add(const QString &name, const QByteArray &data);
    Result<void> addFile(const QString &name, const QString &path);
    qint64 size() const { return m_body.size() + m_directory.size() + 22; }
    Result<void> save(const QString &path) const;

  private:
    QByteArray m_body, m_directory;
    int m_count = 0;
};
} // namespace nekotune::zip
