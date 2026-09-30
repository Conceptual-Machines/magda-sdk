cmake_minimum_required(VERSION 3.22)
project(magda_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/BoundaryCheck.cmake)

configure_file(core/magda/sdk/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/magda/sdk/version_config.hpp" @ONLY)

add_library(magda_sdk_core STATIC core/magda/sdk/version.cpp)
add_library(magda::sdk_core ALIAS magda_sdk_core)
target_include_directories(magda_sdk_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>)
target_compile_features(magda_sdk_core PUBLIC cxx_std_23)

if(EMSCRIPTEN)
    target_compile_options(magda_sdk_core PUBLIC -msimd128)
endif()

option(MAGDA_SDK_BUILD_TESTS "Build the SDK tests" ${PROJECT_IS_TOP_LEVEL})
if(MAGDA_SDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
