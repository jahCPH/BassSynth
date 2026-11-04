cmake_minimum_required(VERSION 3.22)

project(BassSynth VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS 13+. Universal binary when not overridden. CI passes arm64 only.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS deployment version")
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures" FORCE)
    endif()
endif()

# JUCE 8.0.12. Locally ThirdParty/JUCE → ../../MyJUCE. CI passes -DJUCE_PATH.
if(NOT JUCE_PATH)
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE" CACHE PATH "Path to JUCE")
endif()
get_filename_component(JUCE_PATH "${JUCE_PATH}" ABSOLUTE)

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${JUCE_PATH}.\n"
        "  Setup: ln -sfn ../../MyJUCE ThirdParty/JUCE\n"
        "  (MyJUCE and BassSynth are siblings under 'Coding projects/')\n"
        "  Or pass -DJUCE_PATH=...")
endif()

message(STATUS "BassSynth: using JUCE at ${JUCE_PATH}")

add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# Default ON for local work. Distribution and CI pass -DBASSSYNTH_DEV_PATHS=OFF
# so a source-tree path cannot ship (Vizuator REL-001). Nothing bakes a path yet.
option(BASSSYNTH_DEV_PATHS
       "Allow source-tree development paths. NEVER enable for distribution."
       ON)

option(BASSSYNTH_BUILD_TESTS "Build the headless DSP test target" ON)

# Identity is name-neutral. A product rename changes PRODUCT_NAME and the
# editor label only. BUNDLE_ID, PLUGIN_CODE, and the manufacturer code stay.
# Cdon is the 42codon-wide manufacturer code (one uppercase letter; Apple
# reserves all-lowercase for AU). Vizuator keeps Vizu. Do not reuse it here.
juce_add_plugin(BassSynth
    COMPANY_NAME "42codon"
    BUNDLE_ID "com.42codon.basssynth"
    PLUGIN_MANUFACTURER_CODE Cdon
    PLUGIN_CODE Bs01
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS AU VST3
    PRODUCT_NAME "PsyRoot")

juce_generate_juce_header(BassSynth)

add_library(psyb_dsp STATIC
    Source/Dsp/ReportedLatency.cpp
    Source/Engine/Sequencer.cpp)

target_include_directories(psyb_dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_features(psyb_dsp PUBLIC cxx_std_17)

target_sources(BassSynth
    PRIVATE
        Source/Plugin/PsybAudioProcessor.cpp
        Source/UI/PsybAudioProcessorEditor.cpp)

target_include_directories(BassSynth PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(BassSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

if(BASSSYNTH_DEV_PATHS)
    target_compile_definitions(BassSynth PRIVATE BASSSYNTH_DEV_PATHS=1)
endif()

target_link_libraries(BassSynth
    PRIVATE
        psyb_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(BASSSYNTH_BUILD_TESTS)
    add_executable(BassSynthTests Tests/SeqTests.cpp)
    target_link_libraries(BassSynthTests PRIVATE psyb_dsp)
    target_compile_features(BassSynthTests PRIVATE cxx_std_17)
endif()
