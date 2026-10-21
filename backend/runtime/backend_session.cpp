#include "runtime/backend_session.h"
#include "app_paths.h"
#include "application/transaction.h"
#include "infrastructure/extensions/extension_lyrics_provider.h"
#include "infrastructure/lyrics/lrclib_provider.h"
#include "infrastructure/lyrics/lyrics_storage.h"
#include "infrastructure/playback/playback_backend_supervisor.h"
#include "ipc/serialization/audio_serialization.h"
#include "storage/resource_repository.h"
#include <QSqlQuery>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <limits>
#include <tuple>
namespace nekotune {
namespace {
AudioSettings savedAudioSettings() {
    const auto parsed = parseAudioSettings(AppPaths::setting("audio_engine").toObject());
    return parsed ? parsed.value() : AudioSettings{};
}
AudioOutputSelection savedAudioOutput() {
    const auto saved = AppPaths::setting("audio_output").toObject();
    return {saved.value("device_id").toString(), saved.value("device_name").toString(),
            saved.value("port_id").toString(), saved.value("port_name").toString()};
}
// Providers are parented before the service is moved to its worker thread.
// That move carries their child network objects into the same thread affinity.
// The controller then owns service shutdown and thread joining as one lifetime unit.
LyricsService *createLyrics() {
    auto *lrclib = new LrclibProvider;
    auto *service = new LyricsService({lrclib}, std::make_unique<LyricsStorage>());
    service->setOffline(AppPaths::setting("lyrics_offline").toBool());
    lrclib->setParent(service);
    return service;
}
} // namespace
// This is the composition root for both desktop and split operation.
// Repositories share the one database session created on this thread.
// Application services receive interfaces, not global adapter lookups.
// IPC routes and event wiring are registered once for this session.
// Artwork enrichment is centralized before snapshots reach any view.
// Worker adapters exchange value snapshots with this backend thread.
// Keep new feature wiring here rather than growing PlayerEngine.
BackendSession::BackendSession(std::unique_ptr<IPlaybackBackend> audio)
    : m_music(m_database), m_songs(m_database), m_queueRepository(m_database),
      m_playlistRepository(m_database), m_tagRepository(m_database), m_historyRepository(m_database),
      m_library(m_songs, m_tagRepository, m_database), m_playlists(m_playlistRepository),
      m_tags(m_tagRepository), m_queue(m_queueRepository, m_songs, m_database),
      m_audio(audio ? std::move(audio)
                    : std::make_unique<PlaybackBackendSupervisor>(savedAudioSettings().backend)),
      m_shuffleStrategies(AppPaths::setting("shuffle_strategy").toString(),
                          [](const QString &id) { return AppPaths::saveSetting("shuffle_strategy", id); }),
      m_player(
          *m_audio, m_queue, std::make_unique<ShuffleBagStrategy>(),
          playbackModeFromString(AppPaths::setting("playback_mode").toString())
              .value_or(PlaybackMode::Sequential),
          [](PlaybackMode mode) { return AppPaths::saveSetting("playback_mode", toString(mode)); },
          AppPaths::setting("playback_rate").toDouble(1.0),
          [](double rate) { return AppPaths::saveSetting("playback_rate", rate); }),
      m_history(m_player, *m_audio, m_historyRepository),
      m_audioOutputs(*m_audio, savedAudioOutput(),
                     [](const AudioOutputSelection &selected) {
                         return AppPaths::saveSetting("audio_output",
                                                      QJsonObject{{"device_id", selected.id},
                                                                  {"device_name", selected.name},
                                                                  {"port_id", selected.portId},
                                                                  {"port_name", selected.portName}});
                     }),
      m_audioSettings(*m_audio, m_player, savedAudioSettings(),
                      [](const AudioSettings &settings) {
                          return AppPaths::saveSetting("audio_engine", audioSettingsJson(settings));
                      }),
      m_lyrics(m_player, createLyrics()), m_covers(std::make_unique<LyricsStorage>()),
      m_collections(m_songs, m_queueRepository, m_playlistRepository, m_database, m_library, m_queue,
                    m_player, &m_music),
      m_api{m_player,    m_queue,   m_library,
            m_playlists, m_tags,    m_collections,
            m_lyrics,    m_imports, m_database.databasePath(),
            m_covers},
      m_scanner(m_imports, m_music, m_library, m_router.commands()),
      m_server(m_router, [this] { return m_api.status(); }),
      m_musicSources(
          m_extensions, m_library, m_imports, m_router.commands(),
          [this](const QString &id, const QString &title) { return m_music.reserveDownload(id, title); }),
      m_maintenance(m_database, m_music, m_imports, m_songs, m_playlistRepository, m_library, m_player,
                    m_router) {
    m_api.shuffleStrategies = &m_shuffleStrategies;
    connect(&m_shuffleStrategies, &ShuffleStrategyService::strategyChanged, this, [this] {
        m_player.setShuffleStrategy(m_shuffleStrategies.createStrategy());
    });
    connect(&m_shuffleStrategies, &ShuffleStrategyService::changed, this, [this] {
        auto event = QJsonObject::fromVariantMap(m_shuffleStrategies.status());
        event.insert("event", "player.shuffle_strategy_changed");
        m_server.broadcastEvent(event);
    });
    connect(&m_extensionBackend, &PluginManager::shuffleCatalogueChanged, this, [this] {
        m_shuffleStrategies.setCatalogue(m_extensionBackend.shuffleStrategies(),
            [backend = QPointer<PluginManager>(&m_extensionBackend)](const QString &id, quint64 generation,
                                                                   const sdk::ShuffleRequest &request) -> Result<sdk::ShuffleSelection> {
                if (!backend) return failure("Shuffle extension unavailable", ErrorCode::Unavailable);
                return backend->selectShuffle(id, generation, request);
            });
    });
    m_maintenance.install();
    connect(&m_maintenance, &LibraryMaintenance::event, &m_server, &IpcServer::broadcastEvent);
    connect(&m_maintenance, &LibraryMaintenance::restoreReady, this, &BackendSession::restoreRequested);
    connect(m_audio.get(), &IPlaybackBackend::sourceReleased, &m_maintenance,
            &LibraryMaintenance::retryCleanup);
    m_covers.updateLyrics(m_lyrics.snapshot());
    m_imports.setMapper([this](const ImportedFile &file) { return m_music.manage(file); });
    m_imports.setRoot(m_music.directory());
    m_imports.setCleanup([this](const ImportedFile &file) {
        QSqlQuery q(m_database.database());
        q.prepare("SELECT 1 FROM song_assets WHERE audio_hash=?");
        q.addBindValue(file.hash);
        if (q.exec() && q.next())
            m_imports.releaseStaging(file.stagingDirectory);
    });
    m_lyrics.setStoredLyricsResolver([this](int id) {
        ResourceRepository assets(m_database);
        const auto song = m_songs.songById(id);
        return LyricsController::StoredLyrics{assets.lyrics(id, "local"), assets.lyrics(id, "selected"),
                                              assets.hasAudio(id) || (song && !song->providerId.isEmpty())};
    });
    m_covers.setStoredCoverResolver([this](const QString &key) {
        ResourceRepository assets(m_database);
        if (!m_resourceIds.contains(key))
            for (const auto &song : m_songs.songs())
                m_resourceIds[song.resourceKey()] = song.id;
        if (m_resourceIds.contains(key)) {
            const auto id = m_resourceIds.value(key);
            const auto path = assets.coverPath(id);
            if (!path.isEmpty())
                return path;
            const auto selected = assets.lyrics(id, "selected");
            if (selected)
                return selected->coverUrl;
        }
        return QString();
    });
    m_sidecars.setWriter([this](const QString &key, const LyricsDocument &doc, const QByteArray &image,
                                const QString &suffix) {
        const auto current = m_player.snapshot().song;
        if (!current || current->metadata.resourceKey() != key)
            return false;
        ImportedFile assets;
        if (!image.isEmpty()) {
            assets.sidecars[suffix] = image;
            if (!m_music.publishArtwork(assets))
                return false;
        }
        Transaction tx(m_database);
        if (!tx)
            return false;
        if (!ResourceRepository(m_database).saveLyrics(current->metadata.id, doc, "selected"))
            return false;
        if (assets.sidecars.contains("cover_hash")) {
            QSqlQuery q(m_database.database());
            q.prepare("INSERT INTO song_assets(song_id,cover_hash) VALUES(?,?) ON "
                      "CONFLICT(song_id) DO UPDATE SET cover_hash=excluded.cover_hash");
            q.addBindValue(current->metadata.id);
            q.addBindValue(QString::fromLatin1(assets.sidecars["cover_hash"]));
            if (!q.exec())
                return false;
        }
        return tx.commit();
    });
    m_covers.setManagedBaseResolver([this](const QString &hash) { return m_music.baseFor(hash); });
    m_lyrics.setManagedBaseResolver([this](const QString &hash) { return m_music.baseFor(hash); });
    m_api.scan = [this] { return m_scanner.start(); };
    m_api.musicDirectory = m_music.directory();
    m_api.configDirectory = AppPaths::configDirectory();
    m_api.ai = &m_ai;
    connect(&m_history, &ListeningHistoryService::changed, this,
            [this] { m_server.broadcastEvent({{"event", "history.changed"}}); });
    connect(&m_history, &ListeningHistoryService::errorOccurred, &m_player, &PlayerEngine::errorOccurred);
    m_api.audioOutputs = &m_audioOutputs;
    m_api.audioSettings = &m_audioSettings;
    m_audioSettings.setAnalysisActions(
        [this](const QVector<int> &ids) -> Result<void> {
            for (int id : ids)
                if (!m_library.metadata(id))
                    return failure("Unknown song ID", ErrorCode::NotFound);
            auto library = m_library.snapshot();
            std::stable_sort(library.songs.begin(), library.songs.end(), [](const auto &a, const auto &b) {
                const auto order = [](int value) {
                    return value > 0 ? value : std::numeric_limits<int>::max();
                };
                return std::tuple(order(a.metadata.discNumber), order(a.metadata.trackNumber),
                                  a.metadata.id) <
                       std::tuple(order(b.metadata.discNumber), order(b.metadata.trackNumber), b.metadata.id);
            });
            QMap<QString, LoudnessAnalysisGroup> grouped;
            QSet<QString> selected, incomplete;
            for (const auto &song : library.songs) {
                const auto &meta = song.metadata;
                if (meta.isRemote())
                    continue;
                const auto album = meta.album.trimmed().toCaseFolded();
                const auto artist =
                    (meta.albumArtist.isEmpty() ? meta.artist : meta.albumArtist).trimmed().toCaseFolded();
                const QString key =
                    album.isEmpty() ? QString("track:%1").arg(meta.id) : album + QChar(0) + artist;
                if (ids.isEmpty() || ids.contains(meta.id))
                    selected.insert(key);
                if (song.path.isEmpty()) {
                    incomplete.insert(key);
                    continue;
                }
                auto &group = grouped[key];
                group.album = !album.isEmpty();
                if (!group.paths.contains(song.path))
                    group.paths.append(song.path);
            }
            QVector<LoudnessAnalysisGroup> groups;
            for (auto it = grouped.begin(); it != grouped.end(); ++it) {
                if (!selected.contains(it.key()))
                    continue;
                auto group = it.value();
                if (incomplete.contains(it.key()))
                    group.album = false;
                groups.append(group);
            }
            return m_loudness.start(groups);
        },
        [this] { m_loudness.cancel(); });
    connect(&m_player, &PlayerEngine::stateChanged, &m_loudness, [this](PlayerState state) {
        m_loudness.setPlaying(state == PlayerState::Playing || state == PlayerState::Loading);
    });
    connect(&m_loudness, &LoudnessAnalysis::changed, this,
            [this] { m_audioSettings.setAnalysisStatus(m_loudness.status()); });
    connect(&m_loudness, &LoudnessAnalysis::resultReady, this,
            [this](const QString &path, const ReplayGainData &data) {
                m_audio->updateReplayGainData(QUrl::fromLocalFile(path), data);
            });
    connect(&m_audioSettings, &AudioSettingsService::changed, this, [this] {
        auto event = m_api.audioSettingsStatus();
        event.insert("event", "player.audio_settings_changed");
        event.insert("playback_rate_supported", m_audio->supportsPlaybackRate());
        event.insert("playback_rate", m_audio->playbackRate());
        m_server.broadcastEvent(event);
    });
    connect(&m_audioOutputs, &AudioOutputService::changed, this, [this] {
        auto event = m_api.audioOutputStatus();
        event.insert("event", "player.audio_outputs_changed");
        m_server.broadcastEvent(event);
    });
    m_api.extensions = &m_extensions;
    m_api.music = &m_musicSources;
    m_player.setSourceResolver(&m_musicSources);
    connect(&m_lyrics, &LyricsController::sourcesChanged, this,
            [this] { m_server.broadcastEvent({{"event", "lyrics.sources_changed"}}); });
    connect(&m_extensions, &ExtensionService::changed, this, [this] {
        QSet<QString> next;
        for (const auto &value : m_extensions.sources("lyrics")) {
            const auto source = value.toMap();
            const auto id = source.value("id").toString();
            next.insert(id);
            if (!m_extensionLyrics.contains(id))
                m_lyrics.addProvider(
                    new ExtensionLyricsProvider(m_extensionBackend, id, source.value("name", id).toString()));
        }
        for (const auto &id : m_extensionLyrics)
            if (!next.contains(id))
                m_lyrics.removeProvider(id);
        m_extensionLyrics = next;
    });
    connect(&m_musicSources, &MusicService::taskChanged, this, [this](const QVariantMap &event) {
        m_server.broadcastEvent(QJsonObject::fromVariantMap(event));
    });
    connect(&m_musicSources, &MusicService::sourcesChanged, this, [this] {
        m_server.broadcastEvent({{"event", "music.sources_changed"}});
        const auto current = m_player.snapshot().song;
        if (current && current->metadata.isRemote() &&
            !m_musicSources.available(current->metadata.providerId))
            m_player.sourceUnavailable(current->metadata.providerId);
    });
    connect(&m_extensions, &ExtensionService::eventReady, this, [this](const QVariantMap &event) {
        if (event.value("event") == "extensions.reloading")
            m_player.sourcesReloading(event.value("ids").toStringList(), event.value("active").toBool());
        m_server.broadcastEvent(QJsonObject::fromVariantMap(event));
    });
    connect(&m_server, &IpcServer::eventPublished, this, [this](const QJsonObject &event) {
        if (!event.value("event").toString().startsWith("extension"))
            m_extensionBackend.publish(event.toVariantMap());
    });
    connect(&m_scanner, &LibraryScanner::finished, this,
            [this](int imported, int skipped, const QStringList &errors) {
                m_server.broadcastEvent({{"event", "library.scan_finished"},
                                         {"imported", imported},
                                         {"skipped", skipped},
                                         {"failed", errors.size()},
                                         {"errors", QJsonArray::fromStringList(errors)}});
            });
    registerPlayerApi(m_router, m_api);
    registerHistoryApi(m_router, m_history);
    registerLibraryApi(m_router, m_api);
    registerLyricsApi(m_router, m_api);
    registerAiApi(m_router, m_api);
    registerExtensionsApi(m_router, m_api);
    registerMusicApi(m_router, m_api);
    auto libraryChanged = [this] {
        m_resourceIds.clear();
        m_server.broadcastEvent({{"event", "library.changed"}});
    };
    auto playlistsChanged = [this] {
        m_server.broadcastEvent({{"event", "playlist.changed"}, {"playlists", m_api.playlistList()}});
    };
    connect(&m_library, &LibraryService::changed, this, libraryChanged);
    connect(&m_tags, &TagService::changed, this, libraryChanged);
    connect(&m_collections, &CollectionService::libraryChanged, this, libraryChanged);
    connect(&m_collections, &CollectionService::playlistsChanged, this, playlistsChanged);
    connect(&m_playlists, &PlaylistService::changed, this, playlistsChanged);
    // Duration enrichment updates every queued occurrence through library
    // identity. Playlist snapshots are also invalidated so all views share the
    // same stored duration. Neither notification selects another track or
    // rebuilds collection membership.
    connect(&m_library, &LibraryService::durationUpdated, &m_player, &PlayerEngine::metadataUpdated);
    connect(&m_library, &LibraryService::durationUpdated, this,
            [playlistsChanged](const SongMetadata &) { playlistsChanged(); });
    connect(&m_library, &LibraryService::metadataChanged, &m_player, &PlayerEngine::metadataUpdated);
    connect(&m_library, &LibraryService::metadataBatchChanged, &m_player,
            &PlayerEngine::metadataBatchUpdated);
    connect(&m_library, &LibraryService::metadataBatchChanged, this,
            [playlistsChanged](const QVector<SongMetadata> &) { playlistsChanged(); });
    connect(&m_library, &LibraryService::statisticsUpdated, &m_queue, &QueueService::updateMetadata);
    connect(&m_library, &LibraryService::statisticsUpdated, this,
            [playlistsChanged](const SongMetadata &) { playlistsChanged(); });
    connect(&m_player, &PlayerEngine::songStarted, this, [this](int id) {
        const auto result = m_library.recordPlayback(id);
        if (!result)
            emit m_player.errorOccurred(result.error().message);
    });
    connect(&m_library, &LibraryService::metadataChanged, this,
            [playlistsChanged](const SongMetadata &) { playlistsChanged(); });
    auto queueChanged = [this] {
        m_server.broadcastEvent({{"event", "queue.changed"}, {"queue", m_api.queueItems()}});
    };
    connect(&m_queue, &QueueService::changed, this, queueChanged);
    connect(&m_player, &PlayerEngine::stateChanged, this, [this](PlayerState state) {
        m_server.broadcastEvent({{"event", "player.state_changed"}, {"state", toString(state)}});
    });
    connect(&m_player, &PlayerEngine::positionChanged, this, [this](qint64 position, qint64 duration) {
        m_server.broadcastEvent(
            {{"event", "player.position_changed"}, {"position", position}, {"duration", duration}});
    });
    connect(&m_player, &PlayerEngine::durationChanged, this, [this](qint64 duration) {
        m_server.broadcastEvent({{"event", "player.duration_changed"}, {"duration", duration}});
    });
    connect(&m_player, &PlayerEngine::seeked, this, [this](qint64 position, int queueId) {
        m_server.broadcastEvent({{"event", "player.seeked"}, {"position", position}, {"queue_id", queueId}});
    });
    connect(&m_player, &PlayerEngine::capabilitiesChanged, this, [this] {
        m_server.broadcastEvent({{"event", "player.capabilities_changed"},
                                 {"capabilities", toJson(m_player.snapshot().capabilities)}});
    });
    connect(&m_player, &PlayerEngine::playbackModeChanged, this, [this](PlaybackMode mode) {
        m_server.broadcastEvent({{"event", "player.playback_mode_changed"},
                                 {"playback_mode", toString(mode)}});
    });
    connect(&m_player, &PlayerEngine::volumeChanged, this, [this](double volume) {
        m_server.broadcastEvent({{"event", "player.volume_changed"}, {"volume", volume}});
    });
    connect(&m_player, &PlayerEngine::playbackRateChanged, this, [this](double rate) {
        m_server.broadcastEvent({{"event", "player.playback_rate_changed"}, {"playback_rate", rate},
                                 {"position", m_player.snapshot().position}});
    });
    auto trackChanged = [this] {
        const auto state = m_api.playbackStatus();
        m_server.broadcastEvent(
            {{"event", "player.track_changed"}, {"song", state.value("song")},
             {"position", state.value("position")}, {"duration", state.value("duration")}});
    };
    connect(&m_player, &PlayerEngine::trackChanged, this, trackChanged);
    connect(&m_player, &PlayerEngine::errorOccurred, this, [this](const QString &message) {
        m_server.broadcastEvent({{"event", "player.error"}, {"message", message}});
    });
    // Update the sidecar's accepted identity before handling later assetsReady
    // notifications. The same lyric snapshot also updates cover resolution for
    // every collection view. Request revisions protect both presentation and
    // eventual filesystem publication.
    connect(&m_lyrics, &LyricsController::changed, this, [this](const LyricsSnapshot &state) {
        m_sidecars.setCurrent(state.trackId, state.revision, state.offline);
        m_covers.updateLyrics(state);
        m_server.broadcastEvent({{"event", "lyrics.changed"}, {"lyrics", toJson(state)}});
    });
    connect(&m_lyrics, &LyricsController::assetsReady, this,
            [this](const QString &hash, const LyricsDocument &document, quint64 revision) {
                m_sidecars.save(hash, revision, m_music.baseFor(hash), document);
            });
    connect(&m_sidecars, &SidecarStore::saved, &m_covers, &CoverService::assetsUpdated);
    connect(&m_sidecars, &SidecarStore::failed, this, [this](const QString &hash, const QString &message) {
        m_server.broadcastEvent(
            {{"event", "library.assets_failed"}, {"song_hash", hash}, {"message", message}});
    });
    // Artwork changes affect current track, library, playlists and repeated queue
    // occurrences. Broadcast all their existing snapshot channels instead of
    // giving each view a resolver. This keeps local/offline cover precedence
    // consistent even when only one asset was updated.
    connect(&m_covers, &CoverService::changed, this,
            [libraryChanged, playlistsChanged, queueChanged, trackChanged] {
                trackChanged();
                libraryChanged();
                playlistsChanged();
                queueChanged();
            });
}
BackendSession::~BackendSession() { shutdown(); }
// Database readiness and socket ownership are startup prerequisites.
// Only a successfully listening backend starts automatic discovery.
// The frontend also requests a scan on reconnect; scanner admission
// coalesces that request with this already-active startup scan.
// Listening success does not imply that discovery has finished yet.
bool BackendSession::start() {
    if (!m_database.isReady() || !m_server.listen())
        return false;
    const auto recovered = m_history.recover();
    if (!recovered) {
        m_database.setError(recovered.error().message);
        m_server.shutdown();
        return false;
    }
    m_scanner.start();
    QTimer::singleShot(0, &m_maintenance, &LibraryMaintenance::startAutomatic);
    m_extensionBackend.start(
        [this](const QString &method, const QVariantMap &params, IExtensionBackend::Completion done) {
            m_router.dispatch({{"method", method}, {"params", QJsonObject::fromVariantMap(params)}},
                              [done](QJsonObject result) {
                                  if (result.value("status") == "ok")
                                      done(result.value("data").toObject().toVariantMap());
                                  else
                                      done(failure(result.value("message").toString()));
                              });
        });
    return true;
}
QString BackendSession::errorString() const {
    return m_database.errorString().isEmpty() ? m_server.errorString() : m_database.errorString();
}
void BackendSession::shutdown() {
    if (m_stopped)
        return;
    m_stopped = true;
    m_maintenance.shutdown();
    m_loudness.cancel();
    // Finish decoder I/O before revoking the extension proxy's leased URLs.
    m_player.shutdown();
    m_extensionBackend.shutdown();
    m_scanner.shutdown();
    m_sidecars.shutdown();
    // Reject new work, then cancel queued commands before import shutdown completes active callbacks.
    m_server.stopAccepting();
    m_router.shutdown();
    m_aiBackend.shutdown();
    m_imports.shutdown();
    m_server.shutdown();
    m_lyrics.shutdown();
}
} // namespace nekotune
