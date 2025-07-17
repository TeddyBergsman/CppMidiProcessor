# Specifies the minimum version of CMake required.
cmake_minimum_required(VERSION 3.16)
project(CppMidiProcessor VERSION 1.0)

# C++ Standard and Qt setup
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6. This requires CMAKE_PREFIX_PATH on Apple Silicon Macs.
set(CMAKE_PREFIX_PATH "/opt/homebrew") # Use /usr/local for Intel Macs.
find_package(Qt6 REQUIRED COMPONENTS Widgets Xml)


# --- Define the Executable Target ---
# Add all source files to the list to be compiled.
add_executable(CppMidiProcessor
  main.cpp
  mainwindow.h
  mainwindow.cpp
  midiprocessor.h
  midiprocessor.cpp
  PresetData.h
  PresetLoader.h
  PresetLoader.cpp
  RtMidi.h
  RtMidi.cpp
)

# Force the compiler to define the macro that enables macOS CoreMIDI code in RtMidi.
target_compile_definitions(CppMidiProcessor PRIVATE __MACOSX_CORE__)

# --- Link Libraries to the Target ---
# Because we are compiling RtMidi ourselves on a Mac, we must link
# against the native Apple frameworks that RtMidi depends on.
target_link_libraries(CppMidiProcessor
  PRIVATE
  Qt6::Widgets
  Qt6::Xml
  "-framework CoreMIDI"
  "-framework CoreAudio"
  "-framework CoreFoundation"
)