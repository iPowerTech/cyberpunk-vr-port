function(cvr_world_marker_shaders target)
    find_program(CVR_DXC_EXECUTABLE NAMES dxc
        HINTS "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
              "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        REQUIRED)
    set(shader "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/Render/WorldMarkers.hlsl")
    set(embed "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedWorldMarkerShaders.cmake")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}/WorldMarkerShaderCode.hpp")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DDXC=${CVR_DXC_EXECUTABLE}" "-DSOURCE=${shader}" "-DOUTPUT=${output}" -P "${embed}"
        DEPENDS "${shader}" "${embed}" "${CVR_DXC_EXECUTABLE}"
        COMMENT "Compiling native-compatible SM6 world marker vertex shaders"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
endfunction()
