juce_add_plugin(OpenPedal
  PRODUCT_NAME "OpenPedal"
  COMPANY_NAME "OpenPedal"
  BUNDLE_ID "dev.openpedal.plugin"
  PLUGIN_MANUFACTURER_CODE Opdl
  PLUGIN_CODE Oped
  FORMATS VST3
  VST3_CATEGORIES Fx Distortion Modulation Delay
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${OPENPEDAL_COPY_PLUGIN}
  VST3_AUTO_MANIFEST FALSE
)

# Example pedals ship inside the binary and are copied to the user's pedal folder on first run.
file(GLOB OPENPEDAL_BUNDLED_PEDALS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/pedals/examples/*.json)
juce_add_binary_data(OpenPedalBundledPedals
  HEADER_NAME BundledPedals.h
  NAMESPACE BundledPedals
  SOURCES ${OPENPEDAL_BUNDLED_PEDALS})

target_sources(OpenPedal PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp
  SlotParameter.cpp
)

target_compile_definitions(OpenPedal PUBLIC
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(OpenPedal
  PRIVATE
    openpedal_core
    OpenPedalBundledPedals
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)

openpedal_apply_warnings(OpenPedal)
