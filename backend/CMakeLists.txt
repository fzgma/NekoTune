add_library(nekotune_domain STATIC
    domain/extensions/extension_backend.h
    domain/extensions/music_source_catalog.h
    domain/extensions/lyrics_source_catalog.h
    domain/library/file_inspector.h
    domain/lyrics/krc_parser.cpp
    domain/lyrics/lrc_parser.cpp
    domain/lyrics/lyrics_provider.h
    domain/lyrics/lyrics_types.cpp
    domain/playback/playback_backend.h
    domain/playback/player_queue.cpp
    domain/playback/shuffle_strategy.cpp
)
target_include_directories(nekotune_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nekotune_domain PUBLIC nekotune_sdk Qt6::Core ZLIB::ZLIB)

add_library(nekotune_storage STATIC
    storage/resource_repository.cpp
    storage/listening_history_repository.cpp
    storage/database_session.cpp
    storage/playlist_repository.cpp
    storage/queue_repository.cpp
    storage/song_repository.cpp
    storage/tag_repository.cpp
)
target_link_libraries(nekotune_storage PUBLIC nekotune_domain nekotune_paths Qt6::Sql)

add_library(nekotune_providers STATIC
    infrastructure/lyrics/lrclib_provider.cpp
    infrastructure/lyrics/network_diagnostics.cpp
)
target_link_libraries(nekotune_providers PUBLIC nekotune_domain nekotune_paths Qt6::Network)

add_library(nekotune_infrastructure STATIC
    infrastructure/extensions/capabilities/audio_proxy.cpp
    infrastructure/extensions/capabilities/extension_lyrics_provider.cpp
    infrastructure/extensions/capabilities/music_sources.cpp
    infrastructure/extensions/capabilities/lyrics_sources.cpp
    infrastructure/extensions/capabilities/shuffle_strategies.cpp
    infrastructure/extensions/capabilities/service_bus.cpp
    infrastructure/extensions/capabilities/extension_storage.cpp
    infrastructure/extensions/capabilities/extension_secrets.cpp
    infrastructure/extensions/capabilities/extension_log.cpp
    infrastructure/extensions/host_gateway.cpp
    infrastructure/extensions/package/extension_manifest.cpp
    infrastructure/extensions/package/extension_registry.cpp
    infrastructure/extensions/package/json_file.cpp
    infrastructure/extensions/package/package_store.cpp
    infrastructure/extensions/package/semver.cpp
    infrastructure/extensions/package/theme_package.cpp
    infrastructure/extensions/package/zip_archive.cpp
    infrastructure/extensions/plugin_manager.cpp
    infrastructure/extensions/runtime/call_guard.cpp
    infrastructure/extensions/runtime/crash_guard.cpp
    infrastructure/extensions/runtime/dependency_graph.cpp
    infrastructure/extensions/runtime/dev_watcher.cpp
    infrastructure/extensions/runtime/extension_state.cpp
    infrastructure/extensions/runtime/host_context.cpp
    infrastructure/extensions/runtime/native_loader.cpp
    infrastructure/extensions/runtime/registration_scope.cpp
    infrastructure/ai/ai_backend.cpp
    infrastructure/ai/ai_settings.cpp
    infrastructure/credentials/credential_store.cpp
    infrastructure/kugou/kugou_account_session.cpp
    infrastructure/library/audio_duration.cpp
    infrastructure/library/loudness_analysis.cpp
    infrastructure/library/audio_reference.cpp
    infrastructure/library/import_executor.cpp
    infrastructure/library/library_file_io.cpp
    infrastructure/library/library_archive.cpp
    infrastructure/library/music_directory.cpp
    infrastructure/lyrics/lyrics_cache.cpp
    infrastructure/lyrics/lyrics_storage.cpp
    infrastructure/lyrics/sidecar_store.cpp
    infrastructure/playback/audio_output_devices.cpp
    infrastructure/playback/pcm_decoder.cpp
    infrastructure/playback/pcm_renderer.cpp
    infrastructure/playback/pcm_dsp.cpp
    infrastructure/playback/ffmpeg_playback_backend.cpp
    infrastructure/playback/playback_backend_supervisor.cpp
    infrastructure/playback/qt_playback_backend.cpp
)
target_link_libraries(nekotune_infrastructure PUBLIC nekotune_domain nekotune_storage nekotune_paths nekotune_theme_resolution Qt6::Network Qt6::Gui Qt6::Multimedia)
target_link_libraries(nekotune_infrastructure PRIVATE qt6keychain PkgConfig::FFMPEG Threads::Threads)
qt_add_resources(nekotune_infrastructure "nekotune_theme_catalog"
    PREFIX "/nekotune"
    BASE "${CMAKE_SOURCE_DIR}/shared"
    FILES "${CMAKE_SOURCE_DIR}/shared/themes/catalog.json")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(TARGET PkgConfig::PULSE)
        target_sources(nekotune_infrastructure PRIVATE infrastructure/playback/pulse_audio_ports.cpp)
        target_link_libraries(nekotune_infrastructure PUBLIC PkgConfig::PULSE)
        target_compile_definitions(nekotune_infrastructure PUBLIC NEKOTUNE_PULSE_AUDIO_PORTS)
    endif()
    target_sources(nekotune_infrastructure PRIVATE infrastructure/credentials/legacy_credential_store.cpp)
    target_compile_definitions(nekotune_infrastructure PRIVATE NEKOTUNE_LEGACY_SECRET_SERVICE)
    target_link_libraries(nekotune_infrastructure PRIVATE PkgConfig::LIBSECRET Threads::Threads)
endif()

add_library(nekotune_application STATIC
    application/audio/pcm_service.cpp
    application/playback/listening_history_service.cpp
    application/extensions/extension_service.h
    application/extensions/music_service.cpp
    application/ai_service.cpp
    application/command_scheduler.cpp
    application/library/collection_service.cpp
    application/library/download_service.cpp
    application/library/library_service.cpp
    application/library/playlist_service.cpp
    application/library/tag_service.cpp
    application/lyrics/cover_service.cpp
    application/lyrics/lyrics_controller.cpp
    application/lyrics/lyrics_service.cpp
    application/playback/playback_order_service.cpp
    application/playback/shuffle_strategy_service.cpp
    application/playback/audio_output_service.cpp
    application/playback/audio_settings_service.cpp
    application/playback/player_engine.cpp
    application/playback/queue_service.cpp
)
target_link_libraries(nekotune_application PUBLIC nekotune_domain)

add_library(nekotune_ipc STATIC
    ipc/api/history_api.cpp
    ipc/api/extensions_api.cpp
    ipc/api/music_api.cpp
    ipc/ipc_router.cpp
    ipc/ipc_server.cpp
    ipc/api/ai_api.cpp
    ipc/api/api_context.cpp
    ipc/api/library_api.cpp
    ipc/api/lyrics_api.cpp
    ipc/api/player_api.cpp
    ipc/serialization/lyrics_serialization.cpp
    ipc/serialization/serialization.cpp
    ipc/serialization/audio_serialization.cpp
)
target_link_libraries(nekotune_ipc PUBLIC nekotune_application nekotune_paths Qt6::Network)

add_library(nekotune_core STATIC
    runtime/library_maintenance.cpp
    runtime/backend_runtime.cpp
    runtime/backend_session.cpp
    runtime/library_scanner.cpp
)
target_link_libraries(nekotune_core PUBLIC nekotune_ipc nekotune_storage nekotune_providers nekotune_infrastructure)
add_executable(nekotune-backend main.cpp)
target_link_libraries(nekotune-backend PRIVATE nekotune_core)
install(TARGETS nekotune-backend RUNTIME DESTINATION bin)
