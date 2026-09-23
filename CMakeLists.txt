cmake_minimum_required(VERSION 3.22)

project(Batida VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Engine: pure DSP, no plugin or UI code. An INTERFACE library, so each
# consumer (plugin, tests, later the preset factory) compiles it alongside its
# own copy of the JUCE modules.
# ---------------------------------------------------------------------------
add_library(BatidaEngine INTERFACE)

target_sources(BatidaEngine
    INTERFACE
        Source/Engine/Parameters.cpp
        Source/Engine/DefaultKit.cpp
        Source/Engine/Envelope.cpp
        Source/Engine/FmSource.cpp
        Source/Engine/SampleData.cpp
        Source/Engine/SampleSource.cpp
        Source/Engine/Processors.cpp
        Source/Engine/Voice.cpp
        Source/Engine/MidiRouter.cpp
        Source/Engine/Kit.cpp)

target_include_directories(BatidaEngine INTERFACE Source)

target_link_libraries(BatidaEngine INTERFACE juce::juce_audio_formats)

set(BATIDA_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# ---------------------------------------------------------------------------
# Plugin (AU only for now; VST3 comes in phase 7)
# ---------------------------------------------------------------------------
juce_add_plugin(Batida
    COMPANY_NAME "Negative Space"
    BUNDLE_ID com.negativespace.batida
    PLUGIN_MANUFACTURER_CODE Ngsp
    PLUGIN_CODE Btda
    FORMATS AU
    PRODUCT_NAME "Batida"
    DESCRIPTION "Sound-design drum instrument"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Batida
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/UI/PluginEditor.cpp
        Source/UI/ParamControl.cpp
        Source/UI/VoicePages.cpp
        Source/UI/FmPage.cpp)

target_compile_definitions(Batida PUBLIC ${BATIDA_DEFINITIONS})

target_link_libraries(Batida
    PRIVATE
        BatidaEngine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests: unit tests, CPU benchmark, and offline renders straight from the engine
# ---------------------------------------------------------------------------
juce_add_console_app(BatidaTests PRODUCT_NAME "BatidaTests")

target_sources(BatidaTests
    PRIVATE
        tests/TestMain.cpp
        tests/EngineTests.cpp
        tests/Tools.cpp)

target_compile_definitions(BatidaTests PRIVATE ${BATIDA_DEFINITIONS})

target_link_libraries(BatidaTests
    PRIVATE
        BatidaEngine
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Developer tool: screenshots of the editor, rendered offscreen
# ---------------------------------------------------------------------------
juce_add_console_app(BatidaSnapshot PRODUCT_NAME "BatidaSnapshot")

target_sources(BatidaSnapshot
    PRIVATE
        tests/Snapshot.cpp
        Source/Plugin/PluginProcessor.cpp
        Source/UI/PluginEditor.cpp
        Source/UI/ParamControl.cpp
        Source/UI/VoicePages.cpp
        Source/UI/FmPage.cpp)

target_compile_definitions(BatidaSnapshot PRIVATE ${BATIDA_DEFINITIONS} JucePlugin_Name="Batida")

target_link_libraries(BatidaSnapshot
    PRIVATE
        BatidaEngine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)
