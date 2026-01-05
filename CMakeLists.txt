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
find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Multimedia)

# --- Testing ---
include(CTest)
enable_testing()

# --- VirtuosoCore (new library, Stage 1) ---
add_library(VirtuosoCore STATIC
  virtuoso/ontology/OntologyRegistry.h
  virtuoso/ontology/OntologyRegistry.cpp
  virtuoso/constraints/ConstraintsTypes.h
  virtuoso/constraints/IInstrumentDriver.h
  virtuoso/constraints/PianoDriver.h
  virtuoso/constraints/PianoDriver.cpp
  virtuoso/constraints/BassDriver.h
  virtuoso/constraints/BassDriver.cpp
  virtuoso/theory/TheoryEvent.h
  virtuoso/theory/TheoryEvent.cpp
  virtuoso/theory/NegativeHarmony.h
  virtuoso/theory/NegativeHarmony.cpp
)

target_link_libraries(VirtuosoCore PUBLIC Qt6::Core)
target_include_directories(VirtuosoCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(VirtuosoCoreTests
  virtuoso/tests/VirtuosoCoreTests.cpp
)
target_link_libraries(VirtuosoCoreTests PRIVATE VirtuosoCore Qt6::Core)
add_test(NAME VirtuosoCoreTests COMMAND VirtuosoCoreTests)


# --- Define the Executable Target as a macOS App Bundle---
# We add resources.qrc here. CMAKE_AUTORCC will handle it automatically.
add_executable(CppMidiProcessor MACOSX_BUNDLE
  main.cpp
  mainwindow.h
  mainwindow.cpp
  LibraryWindow.h
  LibraryWindow.cpp
  midiprocessor.h
  midiprocessor.cpp
  voicecontroller.h
  voicecontroller.cpp
  PresetData.h
  PresetLoader.h
  PresetLoader.cpp
  RtMidi.h
  RtMidi.cpp
  NoteMonitorWidget.h
  NoteMonitorWidget.cpp
  BassStyleEditorDialog.h
  BassStyleEditorDialog.cpp
  PianoStyleEditorDialog.h
  PianoStyleEditorDialog.cpp
  PitchMonitorWidget.h
  PitchMonitorWidget.cpp
  PitchColor.h
  WaveVisualizer.h
  WaveVisualizer.cpp
  ireal/IRealTypes.h
  ireal/IRealbCodec.h
  ireal/IRealbCodec.cpp
  ireal/HtmlPlaylistParser.h
  ireal/HtmlPlaylistParser.cpp
  chart/ChartModel.h
  chart/IRealProgressionParser.h
  chart/IRealProgressionParser.cpp
  chart/SongChartWidget.h
  chart/SongChartWidget.cpp
  SilentPlaybackEngine.h
  SilentPlaybackEngine.cpp
  playback/BandPlaybackEngine.h
  playback/BandPlaybackEngine.cpp
  music/Pitch.h
  music/Pitch.cpp
  music/ChordSymbol.h
  music/ChordSymbol.cpp
  music/ChordDictionary.h
  music/ChordDictionary.cpp
  music/BassProfile.h
  music/BassProfile.cpp
  music/BassPresets.h
  music/BassPresets.cpp
  music/PianoProfile.h
  music/PianoProfile.cpp
  music/PianoPresets.h
  music/PianoPresets.cpp
  music/ScaleLibrary.h
  music/ScaleLibrary.cpp
  music/WalkingBassGenerator.h
  music/WalkingBassGenerator.cpp
  music/JazzPianoGenerator.h
  music/JazzPianoGenerator.cpp
  music/SelfTest.h
  music/SelfTest.cpp
  virtuoso/ui/GuitarFretboardWidget.h
  virtuoso/ui/GuitarFretboardWidget.cpp
  virtuoso/ui/PianoKeyboardWidget.h
  virtuoso/ui/PianoKeyboardWidget.cpp
  resources.qrc
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
  Qt6::Multimedia
  VirtuosoCore
  "-framework CoreMIDI"
  "-framework CoreAudio"
  "-framework CoreFoundation"
)

# Ensure local headers are always discoverable (clangd / IDE tooling)
target_include_directories(CppMidiProcessor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")