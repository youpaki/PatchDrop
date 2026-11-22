cmake_minimum_required(VERSION 3.22)
project(PatchLink VERSION 1.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PatchLink
  COMPANY_NAME "leve"
  PLUGIN_MANUFACTURER_CODE leve
  PLUGIN_CODE Plnk
  FORMATS VST3 Standalone
  PRODUCT_NAME "PatchLink"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx
  DESCRIPTION "Chainer nodal avec partage de patch"
)

target_sources(PatchLink PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/Chain/PatchFormat.h
  Source/Chain/ShareClient.h
  Source/Core/ChainFormat.h
  Source/Core/DesignTokens.h
  Source/Core/Fonts.h
  Source/Core/Hazard.h
  Source/Scan/DawDetector.h
  Source/Scan/PluginScanManager.h
  Source/Scan/PluginScanManager.cpp
  Source/UI/ScanDialog.h
  Source/UI/ScanDialog.cpp
)

target_compile_definitions(PatchLink PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=1
)

target_link_libraries(PatchLink PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_data_structures
)

juce_generate_juce_header(PatchLink)

# Embedded typefaces (OFL): Archivo Black / Space Grotesk / JetBrains Mono
juce_add_binary_data(PatchLinkAssets SOURCES
  Assets/Fonts/ArchivoBlack-Regular.ttf
  Assets/Fonts/SpaceGrotesk-Variable.ttf
  Assets/Fonts/JetBrainsMono-Variable.ttf
)
target_link_libraries(PatchLink PRIVATE PatchLinkAssets)

# ScanTest: headless scanner check (also useful on user machines for debugging)
juce_add_console_app(ScanTest NEEDS_CURL FALSE)
target_sources(ScanTest PRIVATE
  Source/Scan/test_main.cpp
  Source/Scan/PluginScanManager.h
  Source/Scan/PluginScanManager.cpp
)
target_compile_definitions(ScanTest PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=1
)
target_link_libraries(ScanTest PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_data_structures
)
juce_generate_juce_header(ScanTest)
