cmake_minimum_required(VERSION 3.22)

project(MC707Editor VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(MC707Editor
    PRODUCT_NAME "MC-707 Editor"
    COMPANY_NAME "mc707-editor"
)

# Embeds data/zencore-map.json into the binary (BinaryData::zencore_map_json)
# so the app has a stable, always-available copy without runtime path
# resolution between the build tree and /data. The JSON file itself stays
# the one source of truth -- see CLAUDE.md: "Parameter map is data, never a
# hardcoded address in /src".
juce_add_binary_data(MC707EditorData
    SOURCES
        data/zencore-map.json
)

target_sources(MC707Editor PRIVATE
    src/Main.cpp
    src/ui/MainComponent.cpp
    src/ui/ControlPanelComponent.cpp
    src/ui/TrackStripComponent.cpp
    src/ui/FileTransferComponent.cpp
    src/ui/ClipMatrixComponent.cpp
    src/ui/ToneEditComponent.cpp
    src/files/SdCardLocator.cpp
    src/midi/CCTransmitter.cpp
    src/model/ZenCoreAddressing.cpp
    src/model/ParameterMap.cpp
)

target_compile_definitions(MC707Editor PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MC707Editor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MC707Editor,JUCE_VERSION>"
)

target_link_libraries(MC707Editor PRIVATE
    MC707EditorData
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_core
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
