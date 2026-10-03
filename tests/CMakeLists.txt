include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0)
FetchContent_MakeAvailable(Catch2)

add_executable(magda_sdk_smoke_test smoke_test.cpp)
target_link_libraries(magda_sdk_smoke_test PRIVATE magda::sdk_core)
add_test(NAME magda_sdk_smoke COMMAND magda_sdk_smoke_test)

add_executable(magda_sdk_tests
    test_binary_text.cpp
    test_curve.cpp
    test_curve_evaluation.cpp
    test_device_interface.cpp
    test_parameter_conversion.cpp
    test_parameter_manifest.cpp
    test_state_codec.cpp
    test_state_node.cpp
    test_farbot.cpp
    test_launch_tap.cpp
    test_metering_buffer.cpp
    test_parameter_queue.cpp
    test_peak_data.cpp
    test_preset.cpp
    test_sidechain_bus.cpp
    test_taps.cpp
    test_value_tap.cpp)
target_link_libraries(magda_sdk_tests PRIVATE magda::sdk_core Catch2::Catch2WithMain)
target_include_directories(magda_sdk_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(magda_sdk_tests)
