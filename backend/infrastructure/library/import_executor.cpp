#include "infrastructure/library/import_executor.h"
#include "app_paths.h"
#include "infrastructure/library/audio_duration.h"
#include "infrastructure/library/audio_reference.h"
#include "infrastructure/library/library_file_io.h"
#include "storage/resource_repository.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
namespace nekotune {
ImportExecutor::ImportExecutor()
    : m_worker(new QObject), m_cancelled(std::make_shared<std::atomic_bool>(false)) {
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}
ImportExecutor::~ImportExecutor() { shutdown(); }
void ImportExecutor::shutdown() {
    if (m_cancelled->exchange(true))
        return;
    m_thread.quit();
    m_thread.wait();
    // Worker replies may already be queued; taking pending callbacks makes late replies harmless.
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (const auto &completion : pending)
        completion(failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled));
}
void ImportExecutor::complete(quint64 id, Result<ImportedFile> result) {
    auto completion = m_pending.take(id);
    if (completion)
        completion(std::move(result));
}
// The worker returns an unmanaged value before resource registration.
// The mapper then publishes/registers the content-addressed resource locally.
// Keeping mapping on this thread preserves database connection affinity.
// Mapper failure is delivered through the same inspection completion.
// The caller never receives an apparently managed path after a failed map.
void ImportExecutor::inspect(const QString &path, Completion completion) {
    if (m_mapper && m_root.isEmpty()) {
        completion(failure("Managed import root is not configured"));
        return;
    }
    inspectFile(
        path,
        [this, completion = std::move(completion)](Result<ImportedFile> file) {
            auto managed = file && m_mapper ? m_mapper(file.value()) : std::move(file);
            const auto cleanup = managed ? std::optional(managed.value()) : std::nullopt;
            completion(std::move(managed));
            if (cleanup && m_cleanup)
                QTimer::singleShot(0, this, [this, file = *cleanup] { m_cleanup(file); });
        },
        bool(m_mapper));
}
void ImportExecutor::releaseStaging(const QString &path) {
    if (m_root.isEmpty() || path.isEmpty())
        return;
    QFileInfo item(path);
    const auto directory = item.isDir() ? item.absoluteFilePath() : item.absolutePath();
    const auto relative = QDir(m_root).relativeFilePath(directory);
    if (!relative.startsWith(".staging/import-") && !relative.startsWith(".staging/download-"))
        return;
    if (relative.count('/') != 1 || ResourceRepository::safePath(m_root, relative).isEmpty())
        return;
    QDir(directory).removeRecursively();
}
// Directory discovery only enumerates supported audio candidates.
// Hashing, probing and deletion-ignore checks happen later per file.
// Avoid recursively following directory symlinks or the config tree.
// This prevents cycles and accidental inclusion of application caches.
// The result is delivered once enumeration has completed normally.
// Shutdown can abandon discovery without invoking a stale scan owner.
void ImportExecutor::discover(const QString &directory, std::function<void(QStringList)> completion) {
    QPointer<ImportExecutor> guard(this);
    auto cancelled = m_cancelled;
    QMetaObject::invokeMethod(m_worker, [guard, cancelled, directory, completion] {
        QStringList paths, folders{directory};
        const QStringList suffixes{"mp3", "m4a", "aac", "wav", "flac", "ogg"};
        while (!folders.isEmpty() && !cancelled->load()) {
            const auto folder = folders.takeLast();
            for (const auto &entry :
                 QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (entry.isDir()) {
                    // Do not follow directory links outside the tree or scan app-owned configuration.
                    if (!entry.isSymLink() && entry.fileName() != "config")
                        folders.append(entry.absoluteFilePath());
                } else if (suffixes.contains(entry.suffix().toLower()) ||
                           entry.fileName().endsWith(".audio.json", Qt::CaseInsensitive))
                    paths.append(entry.absoluteFilePath());
            }
        }
        if (!guard || cancelled->load())
            return;
        QMetaObject::invokeMethod(guard, [guard, completion, paths] {
            if (guard)
                completion(paths);
        });
    });
}
// Capture size and modification time before hashing and probing.
// Both operations use the same file path but can take significant time.
// The final metadata check catches detectable changes during that work.
// Hash identity describes audio bytes rather than its displayed filename.
// Probe failure is non-fatal and produces an unknown duration value.
// Cancellation is checked during chunks and after probing returns.
// Only the owning thread removes a pending completion from the map.
void ImportExecutor::inspectUnmanaged(const QString &path, Completion completion) {
    inspectFile(path, std::move(completion), false);
}
void ImportExecutor::inspectFile(const QString &path, Completion completion, bool copy, bool probe) {
    if (m_cancelled->load()) {
        completion(failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled));
        return;
    }
    QPointer<ImportExecutor> guard(this);
    auto cancelled = m_cancelled;
    const auto id = m_nextId++;
    m_pending.insert(id, std::move(completion));
    const auto root = m_root.isEmpty() ? AppPaths::musicDirectory() : m_root;
    QMetaObject::invokeMethod(m_worker, [guard, cancelled, path, id, copy, probe, root]() {
        auto inspect = [&]() -> Result<ImportedFile> {
            std::optional<AudioReference> reference;
            if (path.endsWith(".audio.json", Qt::CaseInsensitive)) {
                auto result = readAudioReference(path);
                if (!result)
                    return result.error();
                reference = result.value();
            }
            QFileInfo info(reference ? reference->path : path);
            if (!info.isFile())
                return failure(QStringLiteral("File does not exist: %1").arg(path), ErrorCode::Io);
            auto size = info.size();
            auto modified = info.lastModified();
            const auto sourcePath = info.absoluteFilePath();
            std::unique_ptr<QTemporaryDir> staging;
            QString copiedHash;
            if (copy) {
                if (!QDir().mkpath(QDir(root).filePath(".staging")))
                    return failure("Cannot create staging directory", ErrorCode::Io);
                staging = std::make_unique<QTemporaryDir>(QDir(root).filePath(".staging/import-XXXXXX"));
                if (!staging->isValid())
                    return failure("Cannot stage audio", ErrorCode::Io);
                const auto target = staging->filePath("audio." + info.suffix().toLower());
                auto copied = LibraryFileIO::copyFile(sourcePath, target, cancelled.get());
                if (!copied)
                    return copied.error();
                copiedHash = copied.value();
                info = QFileInfo(target);
            }
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly))
                return failure(QStringLiteral("Unable to read file: %1").arg(path), ErrorCode::Io);
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!copy && !file.atEnd()) {
                if (cancelled->load())
                    return failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled);
                auto chunk = file.read(1024 * 1024);
                if (file.error() != QFileDevice::NoError)
                    return failure(file.errorString(), ErrorCode::Io);
                hash.addData(chunk);
            }
            const auto audio =
                probe ? readAudioFileInfo(info.absoluteFilePath(), *cancelled) : AudioFileInfo{};
            if (cancelled->load())
                return failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled);
            info.refresh();
            // Reject a hash/duration pair collected while the input was being replaced or written.
            if (!copy && (info.size() != size || info.lastModified() != modified))
                return failure(QStringLiteral("File changed during import"), ErrorCode::Io);
            const auto audioHash = copy ? copiedHash : QString::fromLatin1(hash.result().toHex());
            if (reference && reference->hash != audioHash)
                return failure("Referenced audio content has changed", ErrorCode::Io);
            auto imported = ImportedFile{info.absoluteFilePath(), audioHash,
                                reference ? reference->sourceName
                                          : info.isSymLink() ? QFileInfo(info.symLinkTarget()).completeBaseName()
                                                             : info.completeBaseName(),
                                audio.durationMs, reference ? QFileInfo(path).absoluteFilePath() : QString()};
            imported.metadata = audio.metadata;
            imported.sourcePath = sourcePath;
            imported.sourceName =
                reference ? reference->sourceName
                          : QFileInfo(QFileInfo(sourcePath).canonicalFilePath()).completeBaseName();
            // Copy sidecars from the original while it is still accessible. A
            // managed sidecar wins over one beside the target of a legacy
            // symlink.
            QStringList bases;
            const QFileInfo original(sourcePath);
            if (original.isSymLink())
                bases << QFileInfo(original.canonicalFilePath()).absolutePath() + '/' +
                             QFileInfo(original.canonicalFilePath()).completeBaseName();
            bases << original.absolutePath() + '/' + original.completeBaseName();
            if (reference)
                bases << QFileInfo(path).absolutePath() + '/' + QFileInfo(path).fileName().chopped(11);
            if (probe)
                for (const auto &base : bases)
                    for (const auto &suffix : {"krc", "lrc", "txt", "jpg", "jpeg", "png", "webp"}) {
                        QFile sidecar(base + '.' + suffix);
                        if (!sidecar.exists())
                            continue;
                        if (!sidecar.open(QIODevice::ReadOnly) ||
                            sidecar.size() > (QString(suffix) == "krc" ? 16 : 5) * 1024 * 1024)
                            return failure("Cannot collect song sidecar: " + sidecar.fileName(),
                                           ErrorCode::Io);
                        const auto data = sidecar.readAll();
                        if (sidecar.error() != QFileDevice::NoError)
                            return failure("Cannot read song sidecar: " + sidecar.fileName(), ErrorCode::Io);
                        imported.sidecars[suffix] = data;
                    }
            if (staging) {
                imported.stagingDirectory = staging->path();
                const auto journal = LibraryFileIO::writeJson(
                    staging->filePath("import.json"),
                    {{"source", sourcePath}, {"hash", audioHash}, {"stage", "copied"}});
                if (!journal)
                    return journal.error();
                staging->setAutoRemove(false);
            }
            return imported;
        };
        auto result = inspect();
        if (!guard || cancelled->load())
            return;
        QMetaObject::invokeMethod(guard, [guard, id, result = std::move(result)]() mutable {
            if (guard)
                guard->complete(id, std::move(result));
        });
    });
}
} // namespace nekotune
