cmake_minimum_required(VERSION 3.22)
project(magda_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/BoundaryCheck.cmake)

configure_file(core/magda/sdk/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/magda/sdk/version_config.hpp" @ONLY)

add_library(magda_sdk_core STATIC
    core/magda/sdk/version.cpp
    core/magda/sdk/curve/AutomationCurve.cpp
    core/magda/sdk/curve/AutomationCurveSimplifier.cpp
    core/magda/sdk/curve/Curve.cpp
    core/magda/sdk/curve/ModCurve.cpp
    core/magda/sdk/device/ParameterDescriptor.cpp
    core/magda/sdk/device/ParameterManifest.cpp
    core/magda/sdk/preset/Preset.cpp
    core/magda/sdk/state/BinaryText.cpp
    core/magda/sdk/state/StateCodec.cpp
    core/magda/sdk/state/StateNode.cpp
    core/magda/sdk/state/detail/Json.cpp
    core/magda/sdk/state/detail/NumberText.cpp)
add_library(magda::sdk_core ALIAS magda_sdk_core)
target_include_directories(magda_sdk_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>)
target_compile_features(magda_sdk_core PUBLIC cxx_std_23)

# Vendored header-only farbot (MIT): `#include <farbot/...>` resolves through this target.
target_include_directories(magda_sdk_core SYSTEM PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/farbot/include>)

if(EMSCRIPTEN)
    target_compile_options(magda_sdk_core PUBLIC -msimd128)
endif()

option(MAGDA_SDK_BUILD_TESTS "Build the SDK tests" ${PROJECT_IS_TOP_LEVEL})
if(MAGDA_SDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
