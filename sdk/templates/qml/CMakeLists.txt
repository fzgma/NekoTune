cmake_minimum_required(VERSION 3.28)
project(HelloQmlExtension LANGUAGES NONE)

file(READ extension.json manifest)
string(JSON extension_id GET "${manifest}" id)
set(output "${CMAKE_BINARY_DIR}/extensions/${extension_id}")
configure_file(extension.json "${output}/extension.json" COPYONLY)
configure_file(Page.qml "${output}/Page.qml" COPYONLY)
set(package "${CMAKE_BINARY_DIR}/packages/${extension_id}.nekotune.zip")
add_custom_command(OUTPUT "${package}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/packages"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${package}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Package.cmake"
    WORKING_DIRECTORY "${output}"
    DEPENDS "${output}/extension.json" "${output}/Page.qml"
    VERBATIM)
add_custom_target(extension_package ALL DEPENDS "${package}")
