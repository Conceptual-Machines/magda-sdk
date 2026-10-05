# magda_sdk_add_device_module(<name> SOURCES <files...> [LIBRARIES <targets...>])
#
# A device module is the sources that define magda::sdk::abi::moduleDevices() and the devices it
# lists. Natively this adds <name>_render (the parity renderer) and <name>_manifests (manifest and
# WAM descriptor exporter); under Emscripten it adds <name>_wasm, written as <name>.wasm.

function(magda_sdk_add_device_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})

    # OBJECT so moduleDevices() is always linked: only the ABI references it.
    add_library(${name}_module OBJECT ${ARG_SOURCES})
    target_link_libraries(${name}_module PUBLIC magda::sdk_abi ${ARG_LIBRARIES})

    if(EMSCRIPTEN)
        set(_abi_exports
            _magda_module_entry _magda_wam_type_count _magda_wam_type_at _magda_wam_create
            _magda_wam_destroy _magda_wam_prepare _magda_wam_release _magda_wam_reset
            _magda_wam_latency _magda_wam_tail _magda_wam_process _magda_wam_set_param
            _magda_wam_get_param _magda_wam_param_count _magda_wam_param_offered
            _magda_wam_param_descriptor _magda_wam_param_to_real _magda_wam_param_to_normalized
            _magda_wam_midi _magda_wam_midi_out_count _magda_wam_midi_out_at _magda_wam_get_state
            _magda_wam_set_state _magda_wam_take_notifications _magda_wam_take_state_patch
            _magda_wam_get_manifest _magda_wam_last_error _malloc _free)
        list(JOIN _abi_exports "," _exports)
        add_executable(${name}_wasm "${MAGDA_SDK_DIR}/hosts/wam/reactor.cpp")
        target_link_libraries(${name}_wasm PRIVATE ${name}_module)
        set_target_properties(${name}_wasm PROPERTIES OUTPUT_NAME ${name} SUFFIX ".wasm")
        target_link_options(${name}_wasm PRIVATE
            --no-entry -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0
            -sEXPORTED_FUNCTIONS=${_exports})
    else()
        add_executable(${name}_render "${MAGDA_SDK_DIR}/hosts/tools/render.cpp")
        target_link_libraries(${name}_render PRIVATE ${name}_module)

        add_executable(${name}_manifests "${MAGDA_SDK_DIR}/hosts/tools/export_manifests.cpp")
        target_link_libraries(${name}_manifests PRIVATE ${name}_module)
    endif()
endfunction()
