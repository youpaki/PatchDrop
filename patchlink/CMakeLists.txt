cmake_minimum_required(VERSION 3.22)
project(PatchLink VERSION 1.0.0)

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
  COMPANY_NAME "PatchLink"
  PLUGIN_MANUFACTURER_CODE Ptlk
  PLUGIN_CODE Plnk
  FORMATS VST3 Standalone
  PRODUCT_NAME "PatchLink"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
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
)

target_compile_definitions(PatchLink PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PatchLink PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_data_structures
)

juce_generate_juce_header(PatchLink)
