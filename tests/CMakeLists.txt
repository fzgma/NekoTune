# Explicit stdout logging keeps QtTest failures visible under Windows runners.
function(nekotune_add_qt_test)
    cmake_parse_arguments(QT_TEST "" "NAME" "COMMAND" ${ARGN})
    add_test(NAME ${QT_TEST_NAME} COMMAND ${QT_TEST_COMMAND}
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/${QT_TEST_NAME}.txt,txt")
endfunction()

set(CMAKE_WIN32_EXECUTABLE OFF)
add_executable(nekotune_library_storage_test library_storage_test.cpp)
target_link_libraries(nekotune_library_storage_test PRIVATE nekotune_core Qt6::Test Qt6::Gui)
nekotune_add_qt_test(NAME nekotune_library_storage_test COMMAND nekotune_library_storage_test)
set_tests_properties(nekotune_library_storage_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
add_executable(nekotune_replay_gain_test replay_gain_test.cpp)
target_link_libraries(nekotune_replay_gain_test PRIVATE nekotune_core Qt6::Test PkgConfig::FFMPEG)
target_compile_definitions(nekotune_replay_gain_test PRIVATE NEKOTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nekotune_add_qt_test(NAME nekotune_replay_gain_test COMMAND nekotune_replay_gain_test)
add_executable(nekotune_listening_history_test listening_history_test.cpp)
target_link_libraries(nekotune_listening_history_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
nekotune_add_qt_test(NAME nekotune_listening_history_test COMMAND nekotune_listening_history_test)
set(NEKOTUNE_TEST_QPA offscreen)
add_executable(nekotune_wallpaper_test wallpaper_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_wallpaper_test PRIVATE nekotune_frontend Qt6::Test Qt6::QuickControls2)
nekotune_add_qt_test(NAME nekotune_wallpaper_test COMMAND nekotune_wallpaper_test)
set_tests_properties(nekotune_wallpaper_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion")
add_executable(nekotune_playback_window_test playback_window_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_playback_window_test PRIVATE nekotune_frontend Qt6::Test Qt6::QuickControls2)
nekotune_add_qt_test(NAME nekotune_playback_window_test COMMAND nekotune_playback_window_test)
set_tests_properties(nekotune_playback_window_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion")
add_executable(nekotune_system_tray_test system_tray_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_system_tray_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
nekotune_add_qt_test(NAME nekotune_system_tray_test COMMAND nekotune_system_tray_test)
set_tests_properties(nekotune_system_tray_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
add_executable(nekotune_system_media_test system_media_test.cpp)
target_link_libraries(nekotune_system_media_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
nekotune_add_qt_test(NAME nekotune_system_media_test COMMAND nekotune_system_media_test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(nekotune_mpris_test mpris_test.cpp)
    target_link_libraries(nekotune_mpris_test PRIVATE nekotune_core nekotune_frontend Qt6::DBus Qt6::Test)
    find_program(NEKOTUNE_DBUS_RUN_SESSION dbus-run-session REQUIRED)
    nekotune_add_qt_test(NAME nekotune_mpris_test COMMAND ${NEKOTUNE_DBUS_RUN_SESSION} -- $<TARGET_FILE:nekotune_mpris_test>
        metadataAndWireTypes playbackRateIsReadOnlyAndFollowsBackend playbackNavigationSeekAndVolume stalePositionsAndErrors
        progressSignalsAndDeferredSeekConfirmation unavailableOutputAndMetadataClear collisionAndCleanup
        disconnectReconnectChangesTrackIdentity raiseAndQuitRequests commandTimeoutAndLateReply
        ipcValidationAndLegacyNavigation playerctlInteroperability backendFailureRetainsSelection
        pendingCommandDisconnectDoesNotReplay)
    nekotune_add_qt_test(NAME nekotune_mpris_without_bus_test COMMAND nekotune_mpris_test withoutSessionBus)
    set_tests_properties(nekotune_mpris_without_bus_test PROPERTIES
        ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/nekotune-test-bus")
endif()
if(WIN32)
    set(NEKOTUNE_TEST_QPA windows)
    set_tests_properties(nekotune_wallpaper_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion")
    set_tests_properties(nekotune_playback_window_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion")
    set_tests_properties(nekotune_system_tray_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software")
endif()

add_executable(nekotune_player_queue_test
    player_queue_test.cpp
)

target_link_libraries(nekotune_player_queue_test
    PRIVATE
        nekotune_core
        Qt6::Test
)

nekotune_add_qt_test(NAME nekotune_player_queue_test COMMAND nekotune_player_queue_test)

add_executable(nekotune_song_store_test
    song_store_test.cpp
)

target_link_libraries(nekotune_song_store_test
    PRIVATE
        nekotune_core
        Qt6::Test
)

nekotune_add_qt_test(NAME nekotune_song_store_test COMMAND nekotune_song_store_test)

add_executable(nekotune_music_directory_test music_directory_test.cpp)
target_link_libraries(nekotune_music_directory_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
target_compile_definitions(nekotune_music_directory_test PRIVATE NEKOTUNE_BACKEND_BINARY="$<TARGET_FILE:nekotune-backend>")
add_dependencies(nekotune_music_directory_test nekotune-backend)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    nekotune_add_qt_test(NAME nekotune_music_directory_test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/with_test_keyring.sh $<TARGET_FILE:nekotune_music_directory_test>)
    set_tests_properties(nekotune_music_directory_test PROPERTIES SKIP_RETURN_CODE 77)
else()
    # Portable file-reference coverage does not require access to a native credential store.
    nekotune_add_qt_test(NAME nekotune_audio_reference_test COMMAND nekotune_music_directory_test
        nativeAudioLinksExposeRealBytes)

endif()

add_executable(nekotune_lyrics_test lyrics_test.cpp)
target_link_libraries(nekotune_lyrics_test PRIVATE nekotune_core Qt6::Test)
nekotune_add_qt_test(NAME nekotune_lyrics_test COMMAND nekotune_lyrics_test)

add_executable(nekotune_cover_test cover_test.cpp)
target_link_libraries(nekotune_cover_test PRIVATE nekotune_core Qt6::Test)
nekotune_add_qt_test(NAME nekotune_cover_test COMMAND nekotune_cover_test)

find_package(Qt6 6.6 REQUIRED COMPONENTS QuickTest)

add_executable(nekotune_slider_quick_test slider_quick_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_slider_quick_test PRIVATE nekotune_frontend Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(nekotune_slider_quick_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
nekotune_add_qt_test(NAME nekotune_slider_quick_test COMMAND nekotune_slider_quick_test)
set_tests_properties(nekotune_slider_quick_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=${NEKOTUNE_TEST_QPA};QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
)

add_executable(nekotune_refactor_test refactor_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_refactor_test PRIVATE nekotune_core nekotune_frontend Qt6::Test Qt6::Quick Qt6::QuickControls2)
target_compile_definitions(nekotune_refactor_test PRIVATE NEKOTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nekotune_add_qt_test(NAME nekotune_refactor_test COMMAND nekotune_refactor_test)
set_tests_properties(nekotune_refactor_test PROPERTIES
 ENVIRONMENT "QT_QPA_PLATFORM=${NEKOTUNE_TEST_QPA};QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion")
add_test(NAME nekotune_dependency_boundaries
 COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/dependency_boundaries.cmake)

add_executable(nekotune_credential_store_test credential_store_test.cpp)
target_link_libraries(nekotune_credential_store_test PRIVATE nekotune_core Qt6::Test)
nekotune_add_qt_test(NAME nekotune_credential_store_test COMMAND nekotune_credential_store_test)

add_executable(nekotune_ai_test ai_test.cpp)
target_link_libraries(nekotune_ai_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    nekotune_add_qt_test(NAME nekotune_ai_test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/with_test_keyring.sh $<TARGET_FILE:nekotune_ai_test>)
    set_tests_properties(nekotune_ai_test PROPERTIES SKIP_RETURN_CODE 77)
else()
    nekotune_add_qt_test(NAME nekotune_ai_test COMMAND nekotune_ai_test)
endif()

# Other platforms opt in to creating temporary entries in their native keychain.
option(NEKOTUNE_TEST_NATIVE_KEYCHAIN "Run tests using the native system keychain" OFF)
add_executable(nekotune_system_credential_store_test system_credential_store_test.cpp)
target_link_libraries(nekotune_system_credential_store_test PRIVATE nekotune_core Qt6::Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(nekotune_system_credential_store_test PRIVATE NEKOTUNE_LEGACY_SECRET_SERVICE)
    target_link_libraries(nekotune_system_credential_store_test PRIVATE PkgConfig::LIBSECRET)
    nekotune_add_qt_test(NAME nekotune_system_credential_store_test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/with_test_keyring.sh $<TARGET_FILE:nekotune_system_credential_store_test>)
    set_tests_properties(nekotune_system_credential_store_test PROPERTIES SKIP_RETURN_CODE 77)
elseif(NEKOTUNE_TEST_NATIVE_KEYCHAIN)
    nekotune_add_qt_test(NAME nekotune_system_credential_store_test COMMAND nekotune_system_credential_store_test)
endif()

add_executable(nekotune_audio_duration_test audio_duration_test.cpp)
target_link_libraries(nekotune_audio_duration_test PRIVATE nekotune_core Qt6::Test)
nekotune_add_qt_test(NAME nekotune_audio_duration_test COMMAND nekotune_audio_duration_test)

add_executable(nekotune_playback_order_test playback_order_test.cpp)
target_link_libraries(nekotune_playback_order_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
nekotune_add_qt_test(NAME nekotune_playback_order_test COMMAND nekotune_playback_order_test)

add_executable(nekotune_audio_output_test audio_output_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_audio_output_test PRIVATE nekotune_core nekotune_frontend Qt6::Test Qt6::Quick Qt6::QuickControls2)
nekotune_add_qt_test(NAME nekotune_audio_output_test COMMAND nekotune_audio_output_test)
set_tests_properties(nekotune_audio_output_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${NEKOTUNE_TEST_QPA};QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion")

add_executable(nekotune_extensions_test extensions_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_extensions_test PRIVATE nekotune_core nekotune_frontend Qt6::Test Qt6::Quick Qt6::QuickControls2)
target_compile_definitions(nekotune_extensions_test PRIVATE NEKOTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nekotune_add_qt_test(NAME nekotune_extensions_test COMMAND nekotune_extensions_test)
set_tests_properties(nekotune_extensions_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${NEKOTUNE_TEST_QPA};QT_QUICK_BACKEND=software")
if(NOT NEKOTUNE_NATIVE_EXTENSIONS)
    add_test(NAME nekotune_extension_sdk_test COMMAND "${NEKOTUNE_NODE_BINARY}" --test tests/packages.test.mjs tests/audio.test.mjs tests/runtime.test.mjs tests/themes.test.mjs)
    set_tests_properties(nekotune_extension_sdk_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/extensions" TIMEOUT 120)
endif()
nekotune_add_extension(nekotune_test_faulty_extension
    MANIFEST native_extensions/faulty/extension.json
    SOURCES native_extensions/faulty/plugin.cpp
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-extensions/test.faulty")
add_executable(nekotune_native_extensions_test native_extensions_test.cpp)
target_link_libraries(nekotune_native_extensions_test PRIVATE nekotune_core Qt6::Test)
target_compile_definitions(nekotune_native_extensions_test PRIVATE
    NEKOTUNE_EXAMPLE_EXTENSIONS="${CMAKE_BINARY_DIR}/extensions"
    NEKOTUNE_TEST_EXTENSIONS="${CMAKE_CURRENT_BINARY_DIR}/native-extensions"
    NEKOTUNE_TEST_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}/native_extensions")
add_dependencies(nekotune_native_extensions_test nekotune_example_lyrics nekotune_example_automation
    nekotune_example_custom_ui nekotune_test_faulty_extension)
nekotune_add_qt_test(NAME nekotune_native_extensions_test COMMAND nekotune_native_extensions_test)
set_tests_properties(nekotune_native_extensions_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTEST_DISABLE_STACK_DUMP=1")

add_executable(nekotune_audio_pipeline_test audio_pipeline_test.cpp)
target_link_libraries(nekotune_audio_pipeline_test PRIVATE nekotune_core Qt6::Test PkgConfig::FFMPEG)
target_compile_definitions(nekotune_audio_pipeline_test PRIVATE NEKOTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nekotune_add_qt_test(NAME nekotune_audio_pipeline_test COMMAND nekotune_audio_pipeline_test)

add_executable(nekotune_audio_pipeline_network_test audio_pipeline_network_test.cpp)
target_link_libraries(nekotune_audio_pipeline_network_test PRIVATE nekotune_core Qt6::Test Qt6::Network)
target_compile_definitions(nekotune_audio_pipeline_network_test PRIVATE NEKOTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
nekotune_add_qt_test(NAME nekotune_audio_pipeline_network_test COMMAND nekotune_audio_pipeline_network_test)
