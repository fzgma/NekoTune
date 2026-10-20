# Script mode: archive every file below the working directory into OUTPUT.
if(NOT OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()
file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*")
list(SORT files)
file(REMOVE "${OUTPUT}")
file(ARCHIVE_CREATE OUTPUT "${OUTPUT}" PATHS ${files} FORMAT zip)
