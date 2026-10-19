find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets)
file(GLOB FRONTEND_CONTROLLERS CONFIGURE_DEPENDS src/controllers/*.cpp src/controllers/*.h src/models/*.cpp src/models/*.h)
add_library(nekotune_frontend STATIC src/ipc_client.cpp src/ipc_client.h src/i18n.cpp src/i18n.h src/extensions/extension_view.cpp src/extensions/extension_view.h ${FRONTEND_CONTROLLERS})
target_sources(nekotune_frontend PRIVATE src/platform/system_media_bridge.cpp src/platform/system_media_bridge.h
    src/platform/system_tray_controller.cpp src/platform/system_tray_controller.h
    src/platform/playback_window_controller.cpp src/platform/playback_window_controller.h)
target_sources(nekotune_frontend PRIVATE
    src/wallpaper/wallpaper_manager.cpp src/wallpaper/wallpaper_manager.h
    src/wallpaper/wallpaper_platform.h)
set(NEKOTUNE_WALLPAPER_BACKEND "disabled")
if(NEKOTUNE_ENABLE_WALLPAPER AND WIN32)
    target_sources(nekotune_frontend PRIVATE src/wallpaper/wallpaper_windows.cpp)
    target_link_libraries(nekotune_frontend PRIVATE user32 comctl32)
    set(NEKOTUNE_WALLPAPER_BACKEND "windows")
elseif(NEKOTUNE_ENABLE_WALLPAPER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # LayerShellQt uses QtWayland private ABI. Keep all three from one Qt build.
    # 6.6 provides explicit output binding and per-window shell integration.
    if(Qt6_VERSION VERSION_GREATER_EQUAL "6.10")
        find_package(Qt6WaylandClient QUIET)
        find_package(Qt6WaylandClientPrivate QUIET)
        find_package(LayerShellQt 6.6 QUIET CONFIG)
        if(TARGET LayerShellQt::Interface AND TARGET Qt6::WaylandClientPrivate)
            target_sources(nekotune_frontend PRIVATE src/wallpaper/wallpaper_wayland.cpp)
            target_link_libraries(nekotune_frontend PRIVATE LayerShellQt::Interface Qt6::WaylandClientPrivate)
            set(NEKOTUNE_WALLPAPER_BACKEND "wayland")
        endif()
    endif()
    if(NEKOTUNE_WALLPAPER_BACKEND STREQUAL "disabled")
        message(STATUS "Music wallpaper unavailable: needs Qt >= 6.10 with matching WaylandClient private development files and LayerShellQt >= 6.6. Player remains enabled.")
    endif()
endif()
if(NEKOTUNE_WALLPAPER_BACKEND STREQUAL "disabled")
    target_sources(nekotune_frontend PRIVATE src/wallpaper/wallpaper_unavailable.cpp)
endif()
message(STATUS "NekoTune wallpaper backend: ${NEKOTUNE_WALLPAPER_BACKEND}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/wallpaper-runtime.json"
    CONTENT "{\"backend\":\"${NEKOTUNE_WALLPAPER_BACKEND}\",\"qt\":\"${Qt6_VERSION}\"}\n")
install(FILES "${CMAKE_BINARY_DIR}/wallpaper-runtime.json" DESTINATION .)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.6 REQUIRED COMPONENTS DBus)
    target_sources(nekotune_frontend PRIVATE src/platform/linux/mpris_service.cpp src/platform/linux/mpris_service.h)
    target_link_libraries(nekotune_frontend PUBLIC Qt6::DBus)
    target_compile_definitions(nekotune_frontend PUBLIC NEKOTUNE_MPRIS)
endif()
target_include_directories(nekotune_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nekotune_frontend PUBLIC nekotune_paths Qt6::Core Qt6::Network Qt6::Quick Qt6::Qml Qt6::Widgets)
add_executable(nekotune-qt-qml
    src/main.cpp
    resources.qrc
)

target_link_libraries(nekotune-qt-qml
    PRIVATE
        nekotune_frontend
        Qt6::Core
        Qt6::Network
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
)

# Unified desktop binary.  It embeds the backend and starts the QML client
# from the same process, while retaining the standalone targets above for
# headless/server deployments and replaceable clients.
add_executable(nekotune
    src/main.cpp
    resources.qrc
)

target_compile_definitions(nekotune
    PRIVATE
        NEKOTUNE_EMBED_BACKEND
)

target_include_directories(nekotune
    PRIVATE
        ${CMAKE_SOURCE_DIR}/backend
)

target_link_libraries(nekotune
    PRIVATE
        nekotune_frontend
        nekotune_core
        Qt6::Core
        Qt6::Network
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
)

set_target_properties(nekotune PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
install(TARGETS nekotune nekotune-qt-qml RUNTIME DESTINATION bin)
