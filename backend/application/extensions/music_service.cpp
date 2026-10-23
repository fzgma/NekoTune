#include "application/extensions/music_service.h"
#include <QPointer>
#include <QUuid>
namespace nekotune {
MusicService::MusicService(IMusicSourceCatalog &sources, LibraryService &library, IFileInspector &imports,
                           CommandScheduler &commands, Reserve reserve)
    : m_sources(sources), m_library(library), m_imports(imports), m_commands(commands),
      m_reserve(std::move(reserve)) {
    connect(&sources, &IMusicSourceCatalog::sourcesChanged, this, &MusicService::sourcesChanged);
}
bool MusicService::available(const QString &source) const {
    for (const auto &item : sources())
        if (item.toMap().value("id").toString() == source)
            return true;
    return false;
}
void MusicService::resolve(const SongMetadata &song, ISourceResolver::Completion done) {
    m_sources.resolvePlayback(song.providerId, song.providerTrackId, [done](Result<IMusicSourceCatalog::Playback> result) {
        if (!result)
            done(result.error());
        else
            done(result.value().url);
    });
}
void MusicService::release(const QUrl &url) {
    if (!url.isEmpty())
        m_sources.release(url);
}
void MusicService::search(const QString &source, const QVariantMap &params,
                          IExtensionBackend::Completion done) {
    m_sources.search(source, params.value("query").toString(), params.value("cursor").toString(),
                     [done](Result<sdk::SearchPage> page) {
        if (!page) {
            done(page.error());
            return;
        }
        QVariantList tracks;
        for (const auto &track : page.value().tracks)
            tracks.append(track.toParams());
        QVariantMap result{{"tracks", tracks}};
        if (!page.value().cursor.isEmpty())
            result.insert("cursor", page.value().cursor);
        done(result);
    });
}
void MusicService::track(const QString &source, const QString &id, IExtensionBackend::Completion done) {
    m_sources.track(source, id, [done](Result<sdk::Track> track) {
        done(track ? Result<QVariantMap>(track.value().toParams()) : Result<QVariantMap>(track.error()));
    });
}
void MusicService::cancelDownload(const QString &task, IExtensionBackend::Completion done) {
    if (!m_downloads.contains(task)) {
        done(failure("Download task not found"));
        return;
    }
    m_cancelled.insert(task);
    m_sources.cancelDownload(task);
    done(QVariantMap{});
}

QString MusicService::download(const QString &source, const SongMetadata &song) {
    const auto task = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QPointer<MusicService> guard(this);
    m_downloads.insert(task);
    auto completed = std::make_shared<bool>(false);
    auto report = [guard, task, completed](QString state, const QString &message = {}, int songId = 0,
                                           QVariantMap details = {}) {
        if (!guard || *completed)
            return;
        if (state != "running") {
            *completed = true;
            guard->m_downloads.remove(task);
            if (guard->m_cancelled.remove(task))
                state = "cancelled";
        }
        details.insert("event", "music.download");
        details.insert("task", task);
        details.insert("state", state);
        details.insert("message", message);
        details.insert("song_id", songId);
        emit guard->taskChanged(details);
    };
    report("running");
    // Only reservation and import use the mutation queue; extension callbacks may themselves call host APIs.
    m_commands.submit(
        [this, source, song, guard, report, task](CommandScheduler::Done next) {
            if (m_cancelled.contains(task)) {
                next();
                report("cancelled");
                return;
            }
            auto base = m_reserve("extension/" + song.resourceKey(), song.customTitle);
            next();
            if (!base) {
                report("failed", base.error().message);
                return;
            }
            m_sources.download(
                source, song.providerTrackId, base.value(), task,
                [guard, task](qint64 received, qint64 total) {
                    if (guard)
                        emit guard->taskChanged({{"event", "music.download"}, {"task", task}, {"state", "running"},
                                                 {"received", received}, {"total", total}});
                },
                [guard, song, source, report, task](Result<QString> result) {
                    if (!guard)
                        return;
                    if (guard->m_cancelled.contains(task)) {
                        report("cancelled");
                        return;
                    }
                    if (!result) {
                        report("failed", result.error().message);
                        return;
                    }
                    const auto path = result.value();
                    guard->m_commands.submit(
                        [guard, song, source, path, report, task](CommandScheduler::Done done) {
                            if (!guard) {
                                done();
                                return;
                            }
                            if (guard->m_cancelled.contains(task)) {
                                done();
                                report("cancelled");
                                return;
                            }
                            guard->m_imports.inspect(path, [guard, song, source, path, report, done,
                                                            task](Result<ImportedFile> file) {
                                if (!guard) {
                                    done();
                                    return;
                                }
                                if (guard->m_cancelled.contains(task)) {
                                    done();
                                    report("cancelled");
                                    return;
                                }
                                if (!file) {
                                    done();
                                    report("failed", file.error().message);
                                    return;
                                }
                                const bool existing = guard->m_library.containsAudioHash(file.value().hash);
                                file.value().importSource = source.section('/', 0, 0);
                                if (file.value().metadata.album.isEmpty())
                                    file.value().metadata.album = song.album;
                                auto saved = guard->m_library.importDownloaded(file.value(), song);
                                done();
                                if (!saved) {
                                    report("failed", saved.error().message);
                                    return;
                                }
                                const int songId = saved.value().id;
                                const auto importedPath = file.value().path;
                                if (guard->m_cancelled.contains(task)) {
                                    report("cancelled", {}, songId, {{"path", importedPath}});
                                    return;
                                }
                                guard->m_sources.downloaded(
                                    source, song.providerTrackId, path, songId, existing,
                                    [guard, report, songId, importedPath, path,
                                     hash = file.value().hash](Result<QVariantMap> assets) {
                                        QVariantMap details =
                                            assets ? assets.value()
                                                   : QVariantMap{{"asset_warning", assets.error().message}};
                                        details.insert("path", importedPath);
                                        if (!guard)
                                            return;
                                        guard->m_imports.inspectAssets(
                                            path, [guard, report, songId, hash, path,
                                                   details](Result<ImportedFile> collected) mutable {
                                                if (!guard)
                                                    return;
                                                guard->m_commands.submit(
                                                    [guard, report, songId, hash, path, details,
                                                     collected = std::move(collected)](auto done) mutable {
                                                        if (!guard) {
                                                            done();
                                                            return;
                                                        }
                                                        if (!collected)
                                                            details["asset_warning"] =
                                                                collected.error().message;
                                                        else if (collected.value().hash != hash)
                                                            details["asset_warning"] =
                                                                "Download callback changed "
                                                                "the audio";
                                                        else {
                                                            auto saved = guard->m_library.attachFile(
                                                                songId, collected.value());
                                                            if (!saved)
                                                                details["asset_warning"] =
                                                                    saved.error().message;
                                                            else if (!details.contains("asset_warning"))
                                                                guard->m_imports.releaseStaging(path);
                                                        }
                                                        done();
                                                        report("finished", {}, songId, details);
                                                    });
                                            });
                                    });
                            });
                        },
                        [report] { report("cancelled"); });
                });
        },
        [report] { report("cancelled"); });
    return task;
}
} // namespace nekotune
