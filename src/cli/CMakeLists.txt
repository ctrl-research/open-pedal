juce_add_console_app(open-pedal-render PRODUCT_NAME "open-pedal-render")

target_sources(open-pedal-render PRIVATE render.cpp)

target_link_libraries(open-pedal-render
  PRIVATE
    openpedal_core
    juce::juce_audio_formats
  PUBLIC
    juce::juce_recommended_config_flags
)

openpedal_apply_warnings(open-pedal-render)
