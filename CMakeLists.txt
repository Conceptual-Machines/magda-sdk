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
    core/magda/sdk/analysis/LogMel.cpp
    core/magda/sdk/analysis/TransientDetector.cpp
    core/magda/sdk/curve/AutomationCurve.cpp
    core/magda/sdk/curve/AutomationCurveSimplifier.cpp
    core/magda/sdk/curve/Curve.cpp
    core/magda/sdk/curve/ModCurve.cpp
    core/magda/sdk/curveedit/CurveEditor.cpp
    core/magda/sdk/curveedit/CurveEditorRender.cpp
    core/magda/sdk/device/ParameterDescriptor.cpp
    core/magda/sdk/device/ParameterManifest.cpp
    core/magda/sdk/display/DisplayList.cpp
    core/magda/sdk/dsp/Fft.cpp
    core/magda/sdk/dsp/Window.cpp
    core/magda/sdk/meter/MeterModel.cpp
    core/magda/sdk/meter/MeterPainter.cpp
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
    core/magda/sdk/state/detail/NumberText.cpp
    core/magda/sdk/waveview/WaveformView.cpp)
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

option(MAGDA_SDK_BUILD_EXAMPLES "Build the example device modules" ${PROJECT_IS_TOP_LEVEL})
if(MAGDA_SDK_BUILD_EXAMPLES)
    magda_sdk_add_device_module(example_gain SOURCES examples/gain/ExampleGain.cpp)
endif()

# The UI cores as a wasm reactor for the Canvas 2D demos (docs/hosts.md).
if(EMSCRIPTEN AND PROJECT_IS_TOP_LEVEL)
    set(_ui_demo_exports
        _meter_demo_run_case _meter_demo_live_set_levels _meter_demo_live_advance
        _meter_demo_live_paint _ui_demo_default_colour _curve_demo_run_case
        _curve_demo_live_configure _curve_demo_live_resize _curve_demo_live_pointer
        _curve_demo_live_key _curve_demo_live_indicator _curve_demo_live_cursor
        _curve_demo_live_render _curve_demo_live_points _curve_demo_live_before
        _curve_demo_live_set_points _wave_demo_run_case _wave_demo_live_load
        _wave_demo_live_resize _wave_demo_live_pointer _wave_demo_live_zoom
        _wave_demo_live_set_loop _wave_demo_live_marker _wave_demo_live_playhead
        _wave_demo_live_cursor _wave_demo_live_render _malloc _free)
    list(JOIN _ui_demo_exports "," _ui_demo_export_list)
    add_executable(magda_sdk_ui_demo hosts/canvas/ui_demo.cpp)
    target_link_libraries(magda_sdk_ui_demo PRIVATE magda::sdk_core)
    target_include_directories(magda_sdk_ui_demo PRIVATE tests)
    set_target_properties(magda_sdk_ui_demo PROPERTIES SUFFIX ".wasm")
    target_link_options(magda_sdk_ui_demo PRIVATE
        --no-entry -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0
        -sEXPORTED_FUNCTIONS=${_ui_demo_export_list})
endif()

set(MAGDA_SDK_JUCE_DIR "" CACHE PATH "A JUCE checkout, for the JUCE host tests")
if(MAGDA_SDK_JUCE_DIR AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory("${MAGDA_SDK_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
endif()

option(MAGDA_SDK_BUILD_TESTS "Build the SDK tests" ${PROJECT_IS_TOP_LEVEL})
if(MAGDA_SDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
