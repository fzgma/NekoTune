cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(NekoTuneSDK VERSION 2.1.0 LANGUAGES CXX)
endif()

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
target_compile_features(nekotune_sdk INTERFACE cxx_std_20)
set_target_properties(nekotune_sdk PROPERTIES EXPORT_NAME SDK)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NekoTuneExtension.cmake)

if(PROJECT_NAME STREQUAL "NekoTune" OR PROJECT_NAME STREQUAL "NekoTuneSDK")
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/NekoTuneSDK")
    install(DIRECTORY include/nekotune DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT NekoTuneSDK)
    install(TARGETS nekotune_sdk EXPORT NekoTuneSDKTargets COMPONENT NekoTuneSDK)
    install(EXPORT NekoTuneSDKTargets NAMESPACE NekoTune:: DESTINATION "${config_dir}" COMPONENT NekoTuneSDK)
    set(NEKOTUNE_SDK_HOST_VERSION "")
    if(PROJECT_NAME STREQUAL "NekoTune")
        set(NEKOTUNE_SDK_HOST_VERSION "${PROJECT_VERSION}")
    endif()
    string(REGEX MATCH "^[0-9]+" NEKOTUNE_SDK_COMPILER_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
    configure_file(cmake/build-info.json.in "${CMAKE_CURRENT_BINARY_DIR}/build-info.json" @ONLY)
    configure_file(cmake/NekoTuneSDKBuild.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKBuild.cmake" @ONLY)
    configure_package_config_file(cmake/NekoTuneSDKConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfig.cmake" INSTALL_DESTINATION "${config_dir}")
    # Additive optional capabilities may increase the SDK minor without changing the plugin IID.
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfigVersion.cmake"
        VERSION 2.1.0 COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKConfigVersion.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NekoTuneSDKBuild.cmake"
        cmake/NekoTuneExtension.cmake cmake/NekoTunePackage.cmake cmake/NekoTuneMergePackages.cmake
        DESTINATION "${config_dir}" COMPONENT NekoTuneSDK)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/build-info.json" README.md
        DESTINATION "${CMAKE_INSTALL_DATADIR}/NekoTuneSDK" COMPONENT NekoTuneSDK)
    install(DIRECTORY templates DESTINATION "${CMAKE_INSTALL_DATADIR}/NekoTuneSDK" COMPONENT NekoTuneSDK)
endif()
