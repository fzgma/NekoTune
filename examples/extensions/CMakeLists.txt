# Native example extensions. Each builds into ${CMAKE_BINARY_DIR}/extensions/<id>, which can be
# mounted as a development directory from the extension manager.
nekotune_add_extension(nekotune_example_lyrics MANIFEST lyrics/extension.json SOURCES lyrics/plugin.cpp)
nekotune_add_extension(nekotune_example_automation MANIFEST automation/extension.json SOURCES automation/plugin.cpp)
nekotune_add_extension(nekotune_example_custom_ui
    MANIFEST custom-ui/extension.json
    SOURCES custom-ui/plugin.cpp
    FILES custom-ui/Page.qml custom-ui/Player.qml custom-ui/Shell.qml custom-ui/Toolbar.qml)
