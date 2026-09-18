# Strict warnings for first-party sources only.
#
# JUCE module sources are compiled inside each consuming target, so a target-wide flag would
# also lint JUCE. Instead, openpedal_apply_warnings(<target>) stamps the flags onto the target's
# own source files (including the ones inherited from openpedal_core) as source properties.
if(MSVC)
  set(OPENPEDAL_WARNING_FLAGS /W4 /permissive- /external:W0)
else()
  set(OPENPEDAL_WARNING_FLAGS
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
    -Woverloaded-virtual -Wnull-dereference)
endif()

# Third-party headers are pulled in as system includes so their code never trips our warnings.
set(OPENPEDAL_SYSTEM_INCLUDE_DIRS ${juce_SOURCE_DIR}/modules ${nlohmann_json_SOURCE_DIR}/include)
if(DEFINED catch2_SOURCE_DIR)
  list(APPEND OPENPEDAL_SYSTEM_INCLUDE_DIRS ${catch2_SOURCE_DIR}/src)
endif()
set(OPENPEDAL_SYSTEM_INCLUDE_FLAGS)
foreach(dir IN LISTS OPENPEDAL_SYSTEM_INCLUDE_DIRS)
  if(MSVC)
    list(APPEND OPENPEDAL_SYSTEM_INCLUDE_FLAGS /external:I "${dir}")
  else()
    list(APPEND OPENPEDAL_SYSTEM_INCLUDE_FLAGS -isystem "${dir}")
  endif()
endforeach()

function(openpedal_apply_warnings target)
  get_target_property(own_sources ${target} SOURCES)
  get_target_property(core_sources openpedal_core INTERFACE_SOURCES)
  set(all_sources ${own_sources} ${core_sources})
  list(FILTER all_sources EXCLUDE REGEX "_deps/")
  list(FILTER all_sources EXCLUDE REGEX "NOTFOUND")
  foreach(src IN LISTS all_sources)
    get_filename_component(abs "${src}" ABSOLUTE)
    set_property(SOURCE "${abs}" TARGET_DIRECTORY ${target}
      APPEND PROPERTY COMPILE_OPTIONS ${OPENPEDAL_WARNING_FLAGS} ${OPENPEDAL_SYSTEM_INCLUDE_FLAGS})
  endforeach()
endfunction()
