# openpedal_core is an INTERFACE library carrying sources, following the usual JUCE pattern:
# JUCE modules must be compiled inside each final target (plugin, CLI, tests), so the core
# sources ride along and are compiled once per consumer.
add_library(openpedal_core INTERFACE)

target_sources(openpedal_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/ParamDescriptor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Board.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Chain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PedalDefinition.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/PedalCollection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/BoardLoader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Paths.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/Block.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/GraphPedal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/blocks/Gain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/blocks/Filter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/blocks/Waveshaper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/blocks/Delay.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/blocks/Lfo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/graph/blocks/Utility.cpp
)

target_include_directories(openpedal_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(openpedal_core INTERFACE
  nlohmann_json::nlohmann_json
  juce::juce_audio_basics
  juce::juce_dsp
)

target_compile_definitions(openpedal_core INTERFACE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_MODAL_LOOPS_PERMITTED=0
)
