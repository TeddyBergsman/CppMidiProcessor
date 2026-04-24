cmake_minimum_required(VERSION 3.22)
project(VocalSyncAU VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Find JUCE — set JUCE_DIR to your JUCE installation path
# e.g.: cmake -DJUCE_DIR=/path/to/JUCE ..
if(NOT DEFINED JUCE_DIR)
    # Common install locations
    if(EXISTS "/opt/homebrew/share/juce")
        set(JUCE_DIR "/opt/homebrew/share/juce")
    elseif(EXISTS "$ENV{HOME}/JUCE")
        set(JUCE_DIR "$ENV{HOME}/JUCE")
    elseif(EXISTS "/usr/local/share/juce")
        set(JUCE_DIR "/usr/local/share/juce")
    else()
        message(FATAL_ERROR
            "JUCE not found. Please set JUCE_DIR:\n"
            "  cmake -DJUCE_DIR=/path/to/JUCE ..\n"
            "  or install JUCE to ~/JUCE or /opt/homebrew/share/juce")
    endif()
endif()

# Add JUCE as a subdirectory
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# Plugin target
juce_add_plugin(VocalSyncAU
    PLUGIN_MANUFACTURER_CODE  Vsyn
    PLUGIN_CODE               VcSn
    FORMATS                   AU          # Audio Unit (macOS)
    PRODUCT_NAME              "VocalSync"
    COMPANY_NAME              "VocalSync"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE        # Auto-install to ~/Library/Audio/Plug-Ins/Components/
)

# Source files
target_sources(VocalSyncAU PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/YinPitchDetector.h
    Source/PsolaProcessor.h
)

# Rubber Band (installed via Homebrew)
find_library(RUBBERBAND_LIB rubberband PATHS /opt/homebrew/lib REQUIRED)
target_include_directories(VocalSyncAU PRIVATE /opt/homebrew/include)

# JUCE modules + SoundTouch
target_link_libraries(VocalSyncAU
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        ${RUBBERBAND_LIB}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Required JUCE compile definitions
target_compile_definitions(VocalSyncAU
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)
