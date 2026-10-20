# Header-only extension SDK shared by the host and native extensions.
add_library(nekotune_sdk INTERFACE)
add_library(NekoTune::SDK ALIAS nekotune_sdk)
target_include_directories(nekotune_sdk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nekotune_sdk INTERFACE Qt6::Core)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NekoTuneExtension.cmake)
