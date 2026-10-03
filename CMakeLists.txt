cmake_minimum_required(VERSION 3.22)
project(magda_sdk VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/BoundaryCheck.cmake)

configure_file(core/magda/sdk/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/magda/sdk/version_config.hpp" @ONLY)

# Vendored pffft (BSD-style FFTPACK licence): the FFT behind magda/sdk/dsp/Fft.hpp.
add_library(magda_sdk_pffft STATIC
    core/third_party/pffft/src/pffft.c
    core/third_party/pffft/src/pffft_common.c)
target_include_directories(magda_sdk_pffft PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/pffft/include>
    PRIVATE core/third_party/pffft/include/pffft core/third_party/pffft/src)
# pffft.h declares its API dllimport on Windows unless told the library is static.
target_compile_definitions(magda_sdk_pffft PUBLIC PFFFT_STATIC_DEFINE)
set_target_properties(magda_sdk_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(magda_sdk_pffft PRIVATE _USE_MATH_DEFINES)
else()
    target_compile_options(magda_sdk_pffft PRIVATE -w)
endif()
if(EMSCRIPTEN)
    target_compile_definitions(magda_sdk_pffft PRIVATE PFFFT_ENABLE_WASM)
    target_compile_options(magda_sdk_pffft PRIVATE -msimd128)
endif()

add_library(magda_sdk_core STATIC
    core/magda/sdk/version.cpp
    core/magda/sdk/analysis/TransientDetector.cpp
    core/magda/sdk/curve/AutomationCurve.cpp
    core/magda/sdk/curve/AutomationCurveSimplifier.cpp
    core/magda/sdk/curve/Curve.cpp
    core/magda/sdk/curve/ModCurve.cpp
    core/magda/sdk/device/ParameterDescriptor.cpp
    core/magda/sdk/device/ParameterManifest.cpp
    core/magda/sdk/dsp/Fft.cpp
    core/magda/sdk/dsp/Window.cpp
    core/magda/sdk/mod/ModAdsr.cpp
    core/magda/sdk/mod/ModFollower.cpp
    core/magda/sdk/mod/ModLfo.cpp
    core/magda/sdk/mod/ModRandom.cpp
    core/magda/sdk/peaks/PeakData.cpp
    core/magda/sdk/preset/Preset.cpp
    core/magda/sdk/sequencer/MonoStepSequencer.cpp
    core/magda/sdk/sequencer/PolyStepSequencer.cpp
    core/magda/sdk/sequencer/StepClock.cpp
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
target_link_libraries(magda_sdk_core PRIVATE magda_sdk_pffft)

# Vendored header-only farbot (MIT): `#include <farbot/...>` resolves through this target.
target_include_directories(magda_sdk_core SYSTEM PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/farbot/include>)

if(EMSCRIPTEN)
    target_compile_options(magda_sdk_core PUBLIC -msimd128)
endif()

# The C ABI (docs/abi.md). Whoever links it defines magda::sdk::abi::moduleDevices().
add_library(magda_sdk_abi STATIC core/magda/sdk/abi/DeviceAbi.cpp)
add_library(magda::sdk_abi ALIAS magda_sdk_abi)
target_link_libraries(magda_sdk_abi PUBLIC magda::sdk_core)

set(MAGDA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")
include(cmake/MagdaSdkDeviceModule.cmake)
include(hosts/juce/MagdaSdkJucePlugin.cmake)

option(MAGDA_SDK_BUILD_REFERENCE_MODULE "Build the reference device module" ${PROJECT_IS_TOP_LEVEL})
if(MAGDA_SDK_BUILD_REFERENCE_MODULE)
    magda_sdk_add_device_module(sdk_reference SOURCES tests/abi/ReferenceDevices.cpp)
endif()

option(MAGDA_SDK_BUILD_TESTS "Build the SDK tests" ${PROJECT_IS_TOP_LEVEL})
if(MAGDA_SDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
