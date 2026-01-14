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
find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Concurrent)

# --- Testing ---
include(CTest)
enable_testing()

# --- VirtuosoCore (new library, Stage 1) ---
add_library(VirtuosoCore STATIC
  virtuoso/bass/AmpleBassUprightMapping.h
  virtuoso/bass/AmpleBassUprightMapping.cpp
  virtuoso/util/StableHash.h
  virtuoso/util/DeterminismPolicy.h
  virtuoso/util/StableRng.h
  virtuoso/control/PerformanceWeightsV2.h
  virtuoso/control/PerformanceWeightsV2.cpp
  virtuoso/solver/CspSolver.h
  virtuoso/solver/BeatCostModel.h
  virtuoso/solver/BeatCostModel.cpp
  virtuoso/memory/MotivicMemory.h
  virtuoso/memory/MotivicMemory.cpp
  virtuoso/memory/MotifTransform.h
  virtuoso/memory/MotifTransform.cpp
  virtuoso/piano/PianoPerformancePlan.h
  virtuoso/piano/PianoPerformanceModel.h
  virtuoso/piano/PianoPerformanceModel.cpp
  virtuoso/ontology/OntologyRegistry.h
  virtuoso/ontology/OntologyRegistry.cpp
  virtuoso/vocab/VocabularyRegistry.h
  virtuoso/vocab/VocabularyRegistry.cpp
  virtuoso/constraints/ConstraintsTypes.h
  virtuoso/constraints/IInstrumentDriver.h
  virtuoso/constraints/PianoDriver.h
  virtuoso/constraints/PianoDriver.cpp
  virtuoso/constraints/BassDriver.h
  virtuoso/constraints/BassDriver.cpp
  virtuoso/constraints/DrumDriver.h
  virtuoso/constraints/DrumDriver.cpp
  virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.h
  virtuoso/drums/FluffyAudioJazzDrumsBrushesMapping.cpp
  virtuoso/groove/GrooveGrid.h
  virtuoso/groove/FeelTemplate.h
  virtuoso/groove/GrooveTemplate.h
  virtuoso/groove/GrooveTemplate.cpp
  virtuoso/groove/GrooveRegistry.h
  virtuoso/groove/GrooveRegistry.cpp
  virtuoso/groove/TimingHumanizer.h
  virtuoso/engine/VirtuosoClock.h
  virtuoso/engine/VirtuosoScheduler.h
  virtuoso/engine/VirtuosoScheduler.cpp
  virtuoso/engine/VirtuosoEngine.h
  virtuoso/engine/VirtuosoEngine.cpp
  virtuoso/theory/TheoryEvent.h
  virtuoso/theory/TheoryEvent.cpp
  virtuoso/theory/NegativeHarmony.h
  virtuoso/theory/NegativeHarmony.cpp
  virtuoso/theory/FunctionalHarmony.h
  virtuoso/theory/FunctionalHarmony.cpp
  virtuoso/theory/ScaleSuggester.h
  virtuoso/theory/ScaleSuggester.cpp
)

target_link_libraries(VirtuosoCore PUBLIC Qt6::Core)
target_include_directories(VirtuosoCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(VirtuosoCoreTests
  virtuoso/tests/VirtuosoCoreTests.cpp
)
target_link_libraries(VirtuosoCoreTests PRIVATE VirtuosoCore Qt6::Core)
add_test(NAME VirtuosoCoreTests COMMAND VirtuosoCoreTests)

add_executable(VirtuosoPlaybackTests
  playback/tests/VirtuosoPlaybackTests.cpp
  playback/HarmonyContext.cpp
  playback/InteractionContext.cpp
  playback/AutoWeightController.cpp
  playback/WeightNegotiator.cpp
  playback/LookaheadWindow.cpp
  playback/LookaheadPlanner.cpp
  playback/JointPhrasePlanner.cpp
  playback/JointCandidateModel.cpp
  playback/AgentCoordinator.cpp
  playback/PrePlaybackCache.cpp
  playback/ChordScaleTable.cpp
  playback/KeyAnalyzer.cpp
  playback/JazzBalladBassPlanner.cpp
  playback/JazzBalladPianoPlanner.cpp
  playback/VoicingUtils.cpp
  playback/LhVoicingGenerator.cpp
  playback/RhVoicingGenerator.cpp
  playback/BrushesBalladDrummer.cpp
  playback/SemanticMidiAnalyzer.cpp
  playback/VibeStateMachine.cpp
  playback/BalladReferenceTuning.cpp
  playback/PianoTextureOrchestrator.cpp
  playback/PianoGestures.cpp
  music/Pitch.cpp
  music/ChordSymbol.cpp
)
target_link_libraries(VirtuosoPlaybackTests PRIVATE VirtuosoCore Qt6::Core Qt6::Concurrent)
add_test(NAME VirtuosoPlaybackTests COMMAND VirtuosoPlaybackTests)


# --- Define the Executable Target as a macOS App Bundle---
# We add resources.qrc here. CMAKE_AUTORCC will handle it automatically.
add_executable(CppMidiProcessor MACOSX_BUNDLE
  main.cpp
  mainwindow.h
  mainwindow.cpp
  LibraryWindow.h
  LibraryWindow.cpp
  GrooveLabWindow.h
  GrooveLabWindow.cpp
  VirtuosoPresetInspectorWindow.h
  VirtuosoPresetInspectorWindow.cpp
  VirtuosoVocabularyWindow.h
  VirtuosoVocabularyWindow.cpp
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
  playback/VirtuosoBalladMvpPlaybackEngine.h
  playback/VirtuosoBalladMvpPlaybackEngine.cpp
  playback/HarmonyTypes.h
  playback/HarmonyContext.h
  playback/HarmonyContext.cpp
  playback/InteractionContext.h
  playback/InteractionContext.cpp
  playback/StoryState.h
  playback/JointPhrasePlanner.h
  playback/JointPhrasePlanner.cpp
  playback/JointCandidateModel.h
  playback/JointCandidateModel.cpp
  playback/PrePlaybackCache.h
  playback/PrePlaybackCache.cpp
  playback/ChordScaleTable.h
  playback/ChordScaleTable.cpp
  playback/KeyAnalyzer.h
  playback/KeyAnalyzer.cpp
  playback/PrePlanningDialog.h
  playback/PrePlanningDialog.cpp
  playback/AgentCoordinator.h
  playback/AgentCoordinator.cpp
  playback/LookaheadWindow.h
  playback/LookaheadWindow.cpp
  playback/LookaheadPlanner.h
  playback/LookaheadPlanner.cpp
  playback/TransportTimeline.h
  playback/TransportTimeline.cpp
  playback/AutoWeightController.h
  playback/AutoWeightController.cpp
  playback/WeightNegotiator.h
  playback/WeightNegotiator.cpp
  playback/BalladReferenceTuning.h
  playback/BalladReferenceTuning.cpp
  playback/JazzBalladBassPlanner.h
  playback/JazzBalladBassPlanner.cpp
  playback/JazzBalladPianoPlanner.h
  playback/JazzBalladPianoPlanner.cpp
  playback/VoicingUtils.h
  playback/VoicingUtils.cpp
  playback/LhVoicingGenerator.h
  playback/LhVoicingGenerator.cpp
  playback/RhVoicingGenerator.h
  playback/RhVoicingGenerator.cpp
  playback/BrushesBalladDrummer.h
  playback/BrushesBalladDrummer.cpp
  playback/SemanticMidiAnalyzer.h
  playback/SemanticMidiAnalyzer.cpp
  playback/VibeStateMachine.h
  playback/VibeStateMachine.cpp
  playback/PianoTextureOrchestrator.h
  playback/PianoTextureOrchestrator.cpp
  playback/PianoGestures.h
  playback/PianoGestures.cpp
  music/Pitch.h
  music/Pitch.cpp
  music/ChordSymbol.h
  music/ChordSymbol.cpp
  music/ChordDictionary.h
  music/ChordDictionary.cpp
  music/ScaleLibrary.h
  music/ScaleLibrary.cpp
  music/SelfTest.h
  music/SelfTest.cpp
  virtuoso/ui/GuitarFretboardWidget.h
  virtuoso/ui/GuitarFretboardWidget.cpp
  virtuoso/ui/PianoKeyboardWidget.h
  virtuoso/ui/PianoKeyboardWidget.cpp
  virtuoso/ui/GrooveTimelineWidget.h
  virtuoso/ui/GrooveTimelineWidget.cpp
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
  Qt6::Concurrent
  VirtuosoCore
  "-framework CoreMIDI"
  "-framework CoreAudio"
  "-framework CoreFoundation"
)

# Ensure local headers are always discoverable (clangd / IDE tooling)
target_include_directories(CppMidiProcessor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")