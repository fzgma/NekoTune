if(NOT OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()
file(ARCHIVE_CREATE OUTPUT "${OUTPUT}" PATHS extension.json Page.qml FORMAT zip)
