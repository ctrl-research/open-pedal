# Third-party dependencies, pinned to exact tags.
# Renovate tracks these pins via the regex manager in renovate.json.
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        9.0.2 # renovate: datasource=github-tags depName=juce-framework/JUCE
  GIT_SHALLOW    TRUE)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0 # renovate: datasource=github-tags depName=nlohmann/json
  GIT_SHALLOW    TRUE)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.16.0 # renovate: datasource=github-tags depName=catchorg/Catch2
  GIT_SHALLOW    TRUE)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(JUCE nlohmann_json)

if(OPENPEDAL_BUILD_TESTS)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
