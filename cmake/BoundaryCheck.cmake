# Configure-time boundary check: only hosts/ may reach JUCE, DOM bindings or Tracktion.
# Quoted includes outside hosts/ must name SDK headers ("magda/...").

set(MAGDA_SDK_ALLOWED_QUOTED_PREFIXES "magda/")
set(MAGDA_SDK_FORBIDDEN_INCLUDE_PATTERNS
    "^juce_" "^JuceHeader" "^juce/"
    "^emscripten" "^webgpu/" "^html5"
    "^tracktion")

file(GLOB_RECURSE _sdk_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.in")

foreach(_source ${_sdk_sources})
    file(RELATIVE_PATH _relative "${CMAKE_CURRENT_SOURCE_DIR}" "${_source}")
    if(_relative MATCHES "^(hosts|build[^/]*|cmake-build[^/]*|\\.[^/]*)/")
        continue()
    endif()

    file(STRINGS "${_source}" _includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
    foreach(_line ${_includes})
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1"
            _header "${_line}")

        foreach(_pattern ${MAGDA_SDK_FORBIDDEN_INCLUDE_PATTERNS})
            if(_header MATCHES "${_pattern}")
                message(FATAL_ERROR
                    "\nSDK boundary violation in ${_relative}:\n"
                    "    includes \"${_header}\"\n"
                    "Only hosts/ may include JUCE, DOM bindings or Tracktion.\n")
            endif()
        endforeach()

        if(_line MATCHES "#[ \t]*include[ \t]*\"")
            set(_allowed FALSE)
            foreach(_prefix ${MAGDA_SDK_ALLOWED_QUOTED_PREFIXES})
                if(_header MATCHES "^${_prefix}")
                    set(_allowed TRUE)
                endif()
            endforeach()
            if(NOT _allowed)
                message(FATAL_ERROR
                    "\nSDK boundary violation in ${_relative}:\n"
                    "    includes \"${_header}\"\n"
                    "Outside hosts/, quoted includes may only name SDK headers (magda/).\n")
            endif()
        endif()
    endforeach()
endforeach()
