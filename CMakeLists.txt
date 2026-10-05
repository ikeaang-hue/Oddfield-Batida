cmake_minimum_required(VERSION 3.22)

# Before project(), which otherwise creates an empty entry and builds for the
# macOS doing the building. Empty (from an older configure) counts as unset.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(Batida VERSION 0.9.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Local fixes to JUCE, kept as patches (patches/) and applied to the
# submodule once, when configuring:
#   juce-view-service-clicks: clicks work when a host runs the AU out of
#   process (Logic, through AUHostingService).
find_package(Git REQUIRED)
foreach(patch juce-view-service-clicks)
    set(patch_file ${CMAKE_SOURCE_DIR}/patches/${patch}.patch)
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${patch_file}
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/external/JUCE
                    RESULT_VARIABLE patch_present OUTPUT_QUIET ERROR_QUIET)
    if(NOT patch_present EQUAL 0)
        execute_process(COMMAND ${GIT_EXECUTABLE} apply ${patch_file}
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/external/JUCE
                        RESULT_VARIABLE patch_result)
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Couldn't apply patches/${patch}.patch to external/JUCE")
        endif()
        message(STATUS "Applied patches/${patch}.patch to JUCE")
    endif()
endforeach()

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Engine: pure DSP, no plugin or UI code. An INTERFACE library, so each
# consumer (plugin, tests, snapshot tool, preset factory) compiles it alongside its
# own copy of the JUCE modules.
# ---------------------------------------------------------------------------
add_library(BatidaEngine INTERFACE)

target_sources(BatidaEngine
    INTERFACE
        Source/Engine/Parameters.cpp
        Source/Engine/ParamRange.cpp
        Source/Engine/DefaultKit.cpp
        Source/Engine/Envelope.cpp
        Source/Engine/FmSource.cpp
        Source/Engine/SampleData.cpp
        Source/Engine/SampleSource.cpp
        Source/Engine/Processors.cpp
        Source/Engine/Voice.cpp
        Source/Engine/MidiRouter.cpp
        Source/Engine/Kit.cpp
        Source/Engine/Chain/ChainSettings.cpp
        Source/Engine/Chain/Compressor.cpp
        Source/Engine/Chain/Saturator.cpp
        Source/Engine/Chain/KitChain.cpp
        Source/Engine/Sequencer/Pattern.cpp
        Source/Engine/Sequencer/Sequencer.cpp
        Source/Engine/Sequencer/PatternVary.cpp
        Source/Engine/Sequencer/PatternMidi.cpp
        Source/Engine/Render.cpp
        Source/Engine/BreakKit.cpp
        Source/Engine/Movement/Movement.cpp
        Source/Engine/Movement/Modulators.cpp
        Source/Engine/Movement/Vary.cpp
        Source/Library/PresetFiles.cpp
        Source/Library/SampleLocator.cpp
        Source/Library/Library.cpp
        Source/Library/Validate.cpp
        Source/Library/Reference.cpp
        Source/Analysis/Measure.cpp)

target_include_directories(BatidaEngine INTERFACE Source)

target_link_libraries(BatidaEngine INTERFACE juce::juce_audio_formats juce::juce_dsp juce::juce_events)

# Development defaults (a breakbeat in pattern 1, Mod 1 moving XY Heat). Off
# by default, so every build starts like a release: patterns and modulators
# empty. cmake -B build -DBATIDA_DEMO=ON to turn them on.
option(BATIDA_DEMO "Start with the development demo pattern and modulation" OFF)
if(BATIDA_DEMO)
    set(BATIDA_DEMO_VALUE 1)
else()
    set(BATIDA_DEMO_VALUE 0)
endif()

# Review builds show the preset factory's candidates in LIB, with Keep and
# Reject (SPEC §11). Off by default, like a release.
option(BATIDA_REVIEW "Show the preset factory's review area in LIB" OFF)
if(BATIDA_REVIEW)
    set(BATIDA_REVIEW_VALUE 1)
else()
    set(BATIDA_REVIEW_VALUE 0)
endif()

set(BATIDA_DEFINITIONS
    BATIDA_DEMO=${BATIDA_DEMO_VALUE}
    BATIDA_REVIEW=${BATIDA_REVIEW_VALUE}
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    BATIDA_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Plugin (AU only for now; VST3 comes in phase 11, SPEC §14)
# ---------------------------------------------------------------------------
juce_add_plugin(Batida
    COMPANY_NAME "Oddfield"
    BUNDLE_ID com.oddfield.batida
    PLUGIN_MANUFACTURER_CODE Odfd
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
    COPY_PLUGIN_AFTER_BUILD FALSE) # copied below, once the batida tool is inside it

# The editor, shared by the plugin and the snapshot tool.
set(BATIDA_UI_SOURCES
    Source/Plugin/PluginProcessor.cpp
    Source/UI/Look/Theme.cpp
    Source/UI/Look/Motion.mm
    Source/UI/MouseLog.mm
    Source/UI/Widgets.cpp
    Source/UI/Graphs.cpp
    Source/UI/ParamControl.cpp
    Source/UI/BrowseStrip.cpp
    Source/UI/DraggableNumber.cpp
    Source/UI/Frame.cpp
    Source/UI/KitPage.cpp
    Source/UI/SoundPage.cpp
    Source/UI/SoundOverlays.cpp
    Source/UI/PatternVaryOverlay.cpp
    Source/UI/PatternGrid.cpp
    Source/UI/SeqPage.cpp
    Source/UI/ModPage.cpp
    Source/UI/LibraryController.cpp
    Source/UI/LibraryPage.cpp
    Source/UI/PluginEditor.cpp)

# JetBrains Mono and Martian Mono (SIL Open Font License), embedded.
juce_add_binary_data(BatidaFonts
    SOURCES
        resources/fonts/JetBrainsMono-Regular.ttf
        resources/fonts/JetBrainsMono-Medium.ttf
        resources/fonts/JetBrainsMono-Bold.ttf
        resources/fonts/MartianMono-StdMd.ttf
        resources/fonts/MartianMono-StdxBd.ttf)

# The factory library (resources/factory, written by BatidaFactory), zipped
# at build time and built into the plugin; Batida writes it out on first use.
# resources/factory-version.txt changes with every new factory, so the zip
# (which lists the folder as it builds) is made again even for new files.
file(GLOB_RECURSE BATIDA_FACTORY_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/resources/factory
     ${CMAKE_SOURCE_DIR}/resources/factory/*)
list(FILTER BATIDA_FACTORY_FILES EXCLUDE REGEX "(^|/)\\.")
list(TRANSFORM BATIDA_FACTORY_FILES PREPEND ${CMAKE_SOURCE_DIR}/resources/factory/ OUTPUT_VARIABLE BATIDA_FACTORY_DEPENDS)
set(BATIDA_FACTORY_ZIP ${CMAKE_BINARY_DIR}/factory/Factory.zip)
add_custom_command(OUTPUT ${BATIDA_FACTORY_ZIP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/factory
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR}/resources/factory -DOUTPUT=${BATIDA_FACTORY_ZIP}
            -P ${CMAKE_SOURCE_DIR}/cmake/ZipFactory.cmake
    DEPENDS ${BATIDA_FACTORY_DEPENDS} ${CMAKE_SOURCE_DIR}/resources/factory-version.txt ${CMAKE_SOURCE_DIR}/cmake/ZipFactory.cmake
    COMMENT "Zipping the factory library"
    VERBATIM)
add_custom_target(BatidaFactoryZip DEPENDS ${BATIDA_FACTORY_ZIP})
juce_add_binary_data(BatidaFactoryData HEADER_NAME FactoryData.h NAMESPACE FactoryData SOURCES ${BATIDA_FACTORY_ZIP})
add_dependencies(BatidaFactoryData BatidaFactoryZip)

target_sources(Batida PRIVATE ${BATIDA_UI_SOURCES})

target_compile_definitions(Batida PUBLIC ${BATIDA_DEFINITIONS})

target_link_libraries(Batida
    PRIVATE
        BatidaEngine
        BatidaFonts
        BatidaFactoryData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# The batida command-line tool (SPEC §13): renders, measures and checks
# library files without a DAW. Built next to the plugin and copied into the AU
# at Contents/Helpers/batida, so it's wherever the plugin is installed.
# ---------------------------------------------------------------------------
juce_add_console_app(BatidaCli PRODUCT_NAME "batida")

target_sources(BatidaCli
    PRIVATE
        tools/cli/Main.cpp
        tools/cli/Cli.cpp)

target_compile_definitions(BatidaCli PRIVATE ${BATIDA_DEFINITIONS})
set_target_properties(BatidaCli PROPERTIES OUTPUT_NAME batida)

target_link_libraries(BatidaCli
    PRIVATE
        BatidaEngine
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_dependencies(Batida_AU BatidaCli)
add_custom_command(TARGET Batida_AU POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:Batida_AU>/Contents/Helpers"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:BatidaCli>" "$<TARGET_BUNDLE_DIR:Batida_AU>/Contents/Helpers/batida"
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:Batida_AU>/Contents/Helpers/batida"
    VERBATIM)
# Then JUCE's step: re-sign the bundle (it changed) and copy it to Components.
juce_enable_copy_plugin_step(Batida)

# ---------------------------------------------------------------------------
# Tests: unit tests, CPU benchmark, and offline renders straight from the engine
# ---------------------------------------------------------------------------
juce_add_console_app(BatidaTests PRODUCT_NAME "BatidaTests")

target_sources(BatidaTests
    PRIVATE
        tests/TestMain.cpp
        tests/EngineTests.cpp
        tests/ChainTests.cpp
        tests/SequencerTests.cpp
        tests/MovementTests.cpp
        tests/LibraryTests.cpp
        tests/FactoryTests.cpp
        tests/ProcessorTests.cpp
        tests/SamplingTests.cpp
        tests/VaryReportTests.cpp
        tests/ScriptingTests.cpp
        tests/Tools.cpp
        tools/factory/Analysis.cpp
        tools/cli/Cli.cpp
        ${BATIDA_UI_SOURCES})

target_include_directories(BatidaTests PRIVATE tools/factory tools/cli)
target_compile_definitions(BatidaTests PRIVATE ${BATIDA_DEFINITIONS} BATIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                           JucePlugin_Name="Batida" JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(BatidaTests
    PRIVATE
        BatidaEngine
        BatidaFonts
        BatidaFactoryData
        juce::juce_audio_utils
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
        ${BATIDA_UI_SOURCES})

target_compile_definitions(BatidaSnapshot PRIVATE ${BATIDA_DEFINITIONS} JucePlugin_Name="Batida" JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(BatidaSnapshot
    PRIVATE
        BatidaEngine
        BatidaFonts
        BatidaFactoryData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# Developer tool: the preset factory (SPEC §11). Makes sound candidates for
# review and builds the factory library in resources/factory from the engine.
# ---------------------------------------------------------------------------
juce_add_console_app(BatidaFactory PRODUCT_NAME "BatidaFactory")

target_sources(BatidaFactory
    PRIVATE
        tools/factory/Main.cpp
        tools/factory/Analysis.cpp
        tools/factory/Sounds.cpp
        tools/factory/Kits.cpp)

target_include_directories(BatidaFactory PRIVATE tools/factory)
target_compile_definitions(BatidaFactory PRIVATE ${BATIDA_DEFINITIONS})

target_link_libraries(BatidaFactory
    PRIVATE
        BatidaEngine
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
