cmake_minimum_required(VERSION 3.28)
project(HelloExtension VERSION 1.0.0 LANGUAGES CXX)

find_package(NekoTuneSDK 3 REQUIRED)
nekotune_add_extension(hello_extension
    MANIFEST extension.json
    SOURCES plugin.cpp
    FILES Page.qml)
add_custom_target(extension_package ALL DEPENDS hello_extension_package)

include(CTest)
if(BUILD_TESTING)
    add_executable(plugin_load_test tests/plugin_load_test.cpp)
    target_link_libraries(plugin_load_test PRIVATE NekoTune::SDK)
    add_dependencies(plugin_load_test hello_extension)
    add_test(NAME plugin_load COMMAND plugin_load_test $<TARGET_FILE:hello_extension>)
endif()
