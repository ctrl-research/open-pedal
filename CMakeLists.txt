cmake_minimum_required(VERSION 3.28)

# Release builds pass the tag via -DOPENPEDAL_VERSION=X.Y.Z; local builds report 0.0.0.
set(OPENPEDAL_VERSION "0.0.0" CACHE STRING "Version reported by the plugin and CLI (X.Y.Z)")

project(OpenPedal
  VERSION ${OPENPEDAL_VERSION}
  DESCRIPTION "User-defined guitar pedals and pedalboards as a VST3 plugin"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(OPENPEDAL_BUILD_PLUGIN "Build the VST3 plugin" ON)
option(OPENPEDAL_BUILD_CLI "Build the offline render CLI" ON)
option(OPENPEDAL_BUILD_TESTS "Build unit tests" ON)
option(OPENPEDAL_COPY_PLUGIN "Copy the plugin into the user plugin folder after build" OFF)

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

add_subdirectory(src/core)

if(OPENPEDAL_BUILD_PLUGIN)
  add_subdirectory(src/plugin)
endif()

if(OPENPEDAL_BUILD_CLI)
  add_subdirectory(src/cli)
endif()

if(OPENPEDAL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
