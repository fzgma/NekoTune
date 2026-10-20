# nekotune_add_extension(<target> MANIFEST <extension.json> SOURCES <...> [FILES <...>]
#                        [OUTPUT_DIRECTORY <dir>] [PACKAGE_DIRECTORY <dir>])
#
# Builds a native extension and lays out a directory that can be mounted as a development
# extension: extension.json (with library, qt and apiVersion filled in), lib/<library> and FILES
# copied to the same path relative to the manifest. SOURCES and FILES resolve like add_library
# sources. The default directory is ${CMAKE_BINARY_DIR}/extensions/<id>.
#
# <target>_package writes <PACKAGE_DIRECTORY>/<id>-<platform>.nekotune.zip (default
# ${CMAKE_BINARY_DIR}/packages). Combine per-platform packages into one installable package with
#   cmake -DOUTPUT=<id>.nekotune.zip -DPACKAGES="<linux.zip>;<windows.zip>" -P NekoTuneMergePackages.cmake
include_guard(GLOBAL)
set(_NEKOTUNE_SDK_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "NekoTune SDK CMake scripts")

function(_nekotune_platform_key out)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" system)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" processor)
    if(processor MATCHES "^(amd64|x64|x86_64)$")
        set(processor "x86_64")
    elseif(processor MATCHES "^(aarch64|arm64)$")
        set(processor "arm64")
    endif()
    set(${out} "${system}-${processor}" PARENT_SCOPE)
endfunction()

function(nekotune_add_extension target)
    cmake_parse_arguments(EXT "" "MANIFEST;OUTPUT_DIRECTORY;PACKAGE_DIRECTORY" "SOURCES;FILES" ${ARGN})
    if(NOT EXT_MANIFEST OR NOT EXT_SOURCES)
        message(FATAL_ERROR "nekotune_add_extension requires MANIFEST and SOURCES")
    endif()
    if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$")
        message(FATAL_ERROR "NekoTune extensions support Linux and Windows only")
    endif()
    get_filename_component(manifest "${EXT_MANIFEST}" ABSOLUTE)
    get_filename_component(source_dir "${manifest}" DIRECTORY)
    file(READ "${manifest}" json)
    string(JSON id GET "${json}" id)
    if(NOT EXT_OUTPUT_DIRECTORY)
        set(EXT_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/${id}")
    endif()
    if(NOT EXT_PACKAGE_DIRECTORY)
        set(EXT_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
    endif()

    add_library(${target} MODULE ${EXT_SOURCES})
    target_link_libraries(${target} PRIVATE NekoTune::SDK)
    set_target_properties(${target} PROPERTIES
        AUTOMOC ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        LIBRARY_OUTPUT_DIRECTORY "${EXT_OUTPUT_DIRECTORY}/lib")
    if(MINGW)
        # Plugins must not depend on a separately shipped libstdc++ that may differ from the host's.
        target_link_options(${target} PRIVATE -static-libgcc)
    endif()

    _nekotune_platform_key(platform)
    string(JSON json SET "${json}" apiVersion 2)
    string(JSON json SET "${json}" qt "\"${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR}\"")
    string(JSON json SET "${json}" library "{\"${platform}\": \"lib/$<TARGET_FILE_NAME:${target}>\"}")
    string(JSON json REMOVE "${json}" main)
    set(template "${CMAKE_CURRENT_BINARY_DIR}/${target}.extension.json.in")
    file(WRITE "${template}" "${json}\n")
    file(GENERATE OUTPUT "${EXT_OUTPUT_DIRECTORY}/extension.json" INPUT "${template}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")

    set(copied "")
    foreach(file IN LISTS EXT_FILES)
        get_filename_component(absolute "${file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        file(RELATIVE_PATH relative "${source_dir}" "${absolute}")
        configure_file("${absolute}" "${EXT_OUTPUT_DIRECTORY}/${relative}" COPYONLY)
        list(APPEND copied "${EXT_OUTPUT_DIRECTORY}/${relative}")
    endforeach()

    set(package "${EXT_PACKAGE_DIRECTORY}/${id}-${platform}.nekotune.zip")
    add_custom_command(OUTPUT "${package}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EXT_PACKAGE_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${package}" -P "${_NEKOTUNE_SDK_CMAKE_DIR}/NekoTunePackage.cmake"
        WORKING_DIRECTORY "${EXT_OUTPUT_DIRECTORY}"
        DEPENDS ${target} "${EXT_OUTPUT_DIRECTORY}/extension.json" ${copied}
        COMMENT "Packaging NekoTune extension ${id}"
        VERBATIM)
    add_custom_target(${target}_package DEPENDS "${package}")
endfunction()
