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
    test_block_peak.cpp
    test_biquad.cpp
    test_curve.cpp
    test_curve_editor.cpp
    test_curve_editor_golden.cpp
    test_curve_evaluation.cpp
    test_device_interface.cpp
    test_display_list.cpp
    test_parameter_conversion.cpp
    test_parameter_manifest.cpp
    test_state_codec.cpp
    test_state_node.cpp
    test_farbot.cpp
    test_fft.cpp
    test_lagrange_resampler.cpp
    test_launch_tap.cpp
    test_log_mel.cpp
    test_meter_golden.cpp
    test_meter_model.cpp
    test_metering_buffer.cpp
    test_mod_adsr.cpp
    test_mod_follower.cpp
    test_mod_lfo.cpp
    test_mod_random.cpp
    test_mod_sample_identity.cpp
    test_oversampler.cpp
    test_parameter_queue.cpp
    test_peak_data.cpp
    test_preset.cpp
    test_random.cpp
    test_sidechain_bus.cpp
    test_published_pattern.cpp
    test_step_clock.cpp
    test_step_sequencer_core.cpp
    test_step_sequencer_identity.cpp
    test_taps.cpp
    test_track_measurer.cpp
    test_transient_detector.cpp
    test_value_tap.cpp
    test_waveform_view.cpp
    test_waveform_view_golden.cpp
    test_window.cpp)
target_link_libraries(magda_sdk_tests PRIVATE magda::sdk_core Catch2::Catch2WithMain)
target_include_directories(magda_sdk_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(magda_sdk_tests PRIVATE MAGDA_SDK_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
option(MAGDA_SDK_EXACT_PINS "Check bit-exact output pins (valid only for a Debug build on the capture machine; a Release build differs on random.free)" OFF)
if(MAGDA_SDK_EXACT_PINS)
    target_compile_definitions(magda_sdk_tests PRIVATE MAGDA_SDK_EXACT_PINS)
endif()
add_executable(magda_sdk_abi_tests abi/test_device_abi.cpp)
target_link_libraries(magda_sdk_abi_tests PRIVATE sdk_reference_module Catch2::Catch2WithMain)
if(TARGET example_gain_module)
    add_executable(magda_sdk_example_tests abi/test_example_module.cpp)
    target_link_libraries(magda_sdk_example_tests PRIVATE example_gain_module Catch2::Catch2WithMain)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(magda_sdk_tests)
catch_discover_tests(magda_sdk_abi_tests)
if(TARGET magda_sdk_example_tests)
    catch_discover_tests(magda_sdk_example_tests)
endif()

# The JUCE host over the reference module, when the build has JUCE (MAGDA_SDK_JUCE_DIR).
if(TARGET juce::juce_audio_processors)
    juce_add_console_app(magda_sdk_juce_host_tests)
    target_sources(magda_sdk_juce_host_tests PRIVATE
        "${MAGDA_SDK_DIR}/hosts/juce/tests/test_magda_device_processor.cpp"
        "${MAGDA_SDK_DIR}/hosts/juce/MagdaDeviceProcessor.cpp")
    target_include_directories(magda_sdk_juce_host_tests PRIVATE "${MAGDA_SDK_DIR}/hosts/juce")
    target_compile_definitions(magda_sdk_juce_host_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(magda_sdk_juce_host_tests PRIVATE
        sdk_reference_module juce::juce_audio_processors Catch2::Catch2WithMain)
    catch_discover_tests(magda_sdk_juce_host_tests)
endif()
