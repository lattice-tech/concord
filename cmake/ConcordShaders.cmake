# Wires bgfx shader compilation into the CMake build graph.
#
# Reads src/engine/render/shaders/shaders.manifest.tsv (the single source of
# truth shared with build_shaders.ps1) and emits one add_custom_command per
# shader: shaderc compiles the .sc to SPIR-V and writes the embedded C header
# under include/engine/render/shaders/generated/. Editing a .sc (or any local
# .sh helper, all of which are conservative dependencies of every shader) now
# rebuilds the header and recompiles the engine automatically instead of
# requiring a manual build_shaders.ps1 run.
#
# When shaderc cannot be found (env BGFX_SHADERC or the conventional
# _downloads location next to the repo), a warning is printed and the
# committed generated headers are used as-is, so machines without the bgfx
# toolchain still build.
#
# Usage from the top-level CMakeLists:
#   include(cmake/ConcordShaders.cmake)
#   concord_setup_shaders()                       # defines concord_shaders
#   ...
#   add_dependencies(CEngine concord_shaders)     # when the target exists

function(concord_setup_shaders)
    set(_shaderDir ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render/shaders)
    set(_genDir ${CMAKE_CURRENT_SOURCE_DIR}/include/engine/render/shaders/generated)
    set(_manifest ${_shaderDir}/shaders.manifest.tsv)

    if(DEFINED ENV{BGFX_SHADERC})
        set(_shaderc $ENV{BGFX_SHADERC})
    else()
        set(_shaderc ${CMAKE_CURRENT_SOURCE_DIR}/../_downloads/bgfx-tools-build/cmake/bgfx/shaderc.exe)
    endif()
    if(DEFINED ENV{BGFX_SHADER_INCLUDE})
        set(_bgfxInc $ENV{BGFX_SHADER_INCLUDE})
    else()
        set(_bgfxInc ${CMAKE_CURRENT_SOURCE_DIR}/../_downloads/bgfx.cmake/bgfx/src)
    endif()

    if(NOT EXISTS ${_shaderc} OR NOT IS_DIRECTORY ${_bgfxInc})
        message(WARNING
            "concord: bgfx shaderc not found (looked at '${_shaderc}', include "
            "'${_bgfxInc}'); shader .sc changes will NOT rebuild the embedded "
            "headers. Set BGFX_SHADERC / BGFX_SHADER_INCLUDE to enable.")
        add_custom_target(concord_shaders)
        return()
    endif()

    # Local .sh helper headers are conservative extra dependencies for every
    # shader (over-approximation; avoids fragile depfile path resolution).
    file(GLOB _shaderHelpers CONFIGURE_DEPENDS ${_shaderDir}/*.sh)

    file(STRINGS ${_manifest} _lines)
    set(_outputs "")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "^#" OR _line STREQUAL "")
            continue()
        endif()
        string(REPLACE "\t" ";" _fields "${_line}")
        list(LENGTH _fields _n)
        if(_n LESS 5)
            message(FATAL_ERROR "concord: malformed manifest line: '${_line}'")
        endif()
        list(GET _fields 0 _src)
        list(GET _fields 1 _type)
        list(GET _fields 2 _varying)
        list(GET _fields 3 _defines)
        list(GET _fields 4 _outName)
        if(_outName STREQUAL "-")
            get_filename_component(_outName ${_src} NAME_WE)
        endif()

        set(_out ${_genDir}/${_outName}.bin.h)
        set(_args -f ${_shaderDir}/${_src} -o ${_out}
                  --bin2c ${_outName}_spv --type ${_type}
                  --platform linux -p spirv -O 3
                  -i ${_bgfxInc} -i ${_shaderDir})
        set(_deps ${_shaderDir}/${_src} ${_manifest} ${_shaderHelpers})
        if(NOT _type STREQUAL "compute")
            list(APPEND _args --varyingdef ${_shaderDir}/${_varying})
            list(APPEND _deps ${_shaderDir}/${_varying})
        endif()
        if(NOT _defines STREQUAL "-")
            list(APPEND _args --define ${_defines})
        endif()

        add_custom_command(
            OUTPUT ${_out}
            COMMAND ${_shaderc} ${_args}
            DEPENDS ${_deps}
            WORKING_DIRECTORY ${_shaderDir}
            COMMENT "shaderc ${_outName} (SPIR-V)"
            VERBATIM)
        list(APPEND _outputs ${_out})
    endforeach()

    add_custom_target(concord_shaders DEPENDS ${_outputs})
    message(STATUS "concord: shader build graph enabled (${_shaderc})")
endfunction()
