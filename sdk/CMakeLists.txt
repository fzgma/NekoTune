# Header-only extension SDK shared by the host and native extensions. External extension
# projects can use it from an installed host (find_package(NekoTuneSDK)) or a source tree
# (add_subdirectory(<NekoTune>/sdk nekotune-sdk)).
if(NOT TARGET Qt6::Core)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Core)
endif()
add_library(nekotune_sdk INTERFACE)
add_library(NekoTune::SDK ALIAS nekotune_sdk)
target_include_directories(nekotune_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(nekotune_sdk INTERFACE Qt6::Core)
set_target_properties(nekotune_sdk PROPERTIES EXPORT_NAME SDK)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NekoTuneExtension.cmake)

if(PROJECT_NAME STREQUAL "NekoTune")
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/NekoTuneSDK")
    install(DIRECTORY include/nekotune DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(TARGETS nekotune_sdk EXPORT NekoTuneSDKTargets)
    install(EXPORT NekoTuneSDKTargets NAMESPACE NekoTune:: DESTINATION "${config_dir}")
    configure_package_config_file(cmake/NekoTuneSDKConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfig.cmake" INSTALL_DESTINATION "${config_dir}")
    # The plugin IID carries the ABI major version, so the SDK follows it rather than the app.
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfigVersion.cmake"
        VERSION 2.0.0 COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfigVersion.cmake"
        cmake/NekoTuneExtension.cmake cmake/NekoTunePackage.cmake cmake/NekoTuneMergePackages.cmake
        DESTINATION "${config_dir}")
endif()
