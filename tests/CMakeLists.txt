juce_add_console_app(openpedal_tests PRODUCT_NAME "openpedal_tests")

target_sources(openpedal_tests PRIVATE
  ParamDescriptorTests.cpp
  BoardJsonTests.cpp
  ChainTests.cpp
  PedalJsonTests.cpp
  GraphPedalTests.cpp
  BlockTests.cpp
  PedalCollectionTests.cpp
  ExamplePedalTests.cpp
  BoardLoaderTests.cpp
)

target_compile_definitions(openpedal_tests PRIVATE
  OPENPEDAL_EXAMPLE_PEDALS_DIR="${CMAKE_SOURCE_DIR}/pedals/examples"
  OPENPEDAL_EXAMPLE_BOARDS_DIR="${CMAKE_SOURCE_DIR}/boards/examples"
)

target_link_libraries(openpedal_tests
  PRIVATE
    openpedal_core
    Catch2::Catch2WithMain
  PUBLIC
    juce::juce_recommended_config_flags
)

include(Catch)
catch_discover_tests(openpedal_tests)

openpedal_apply_warnings(openpedal_tests)
