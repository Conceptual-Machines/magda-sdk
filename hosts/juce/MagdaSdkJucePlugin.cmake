# magda_sdk_add_juce_plugin(<target> MODULE <module> DEVICE <type> [EXCLUDE_FROM_ALL]
#                           <juce_add_plugin arguments...>)
#
# A JUCE plugin of one device from a device module (magda_sdk_add_device_module), through the
# C ABI. Call it after JUCE is available; the rest of the arguments go to juce_add_plugin.

function(magda_sdk_add_juce_plugin target)
    cmake_parse_arguments(ARG "EXCLUDE_FROM_ALL" "MODULE;DEVICE" "" ${ARGN})
    juce_add_plugin(${target} ${ARG_UNPARSED_ARGUMENTS})
    target_sources(${target} PRIVATE "${MAGDA_SDK_DIR}/hosts/juce/MagdaDeviceProcessor.cpp")
    target_include_directories(${target} PRIVATE "${MAGDA_SDK_DIR}/hosts/juce")
    target_compile_definitions(${target} PUBLIC
        MAGDA_SDK_PLUGIN_DEVICE_TYPE="${ARG_DEVICE}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target} PRIVATE ${ARG_MODULE}_module juce::juce_audio_utils)

    if(ARG_EXCLUDE_FROM_ALL)
        get_target_property(_formats ${target} JUCE_FORMATS)
        set_target_properties(${target} ${target}_All PROPERTIES EXCLUDE_FROM_ALL TRUE)
        foreach(_format ${_formats})
            if(TARGET ${target}_${_format})
                set_target_properties(${target}_${_format} PROPERTIES EXCLUDE_FROM_ALL TRUE)
            endif()
        endforeach()
    endif()
endfunction()
