cmake_minimum_required(VERSION 3.28)
project(concord)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 第三方库目录约定
#   头文件: src/3rd/<库名>/...
#   库文件: lib/  (静态库 .a / 导入库 .dll.a .lib / 运行时 .dll)

set(CONCORD_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(CONCORD_3RD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/3rd)

# SDL3 3.4.12 (动态库 SDL3.dll + MinGW 导入库 libSDL3.dll.a)
# 头文件: src/3rd/SDL3  ->  #include <SDL3/SDL.h>
add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION "${CONCORD_LIB_DIR}/SDL3.dll"
        IMPORTED_IMPLIB "${CONCORD_LIB_DIR}/libSDL3.dll.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CONCORD_3RD_DIR}")

# Steam Audio 4.8.1 (动态库 phonon.dll + 导入库 phonon.lib)
# 头文件: src/3rd/phonon  ->  #include <phonon.h>
add_library(Phonon::Phonon SHARED IMPORTED GLOBAL)
set_target_properties(Phonon::Phonon PROPERTIES
        IMPORTED_LOCATION "${CONCORD_LIB_DIR}/phonon.dll"
        IMPORTED_IMPLIB "${CONCORD_LIB_DIR}/phonon.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CONCORD_3RD_DIR}/phonon")

# bgfx / bimg / bx (静态库, MinGW x64 Release, bgfx.cmake 构建)
# 头文件: src/3rd/bgfx, src/3rd/bimg, src/3rd/bx,
#         src/3rd/compat, src/3rd/tinystl
# 用法:  #include <bgfx/bgfx.h> / <bx/bx.h> / <bimg/bimg.h>
if(MINGW)
    set(BX_COMPAT_PLATFORM mingw)
elseif(WIN32)
    set(BX_COMPAT_PLATFORM msvc)
endif()

add_library(bx STATIC IMPORTED GLOBAL)
set_target_properties(bx PROPERTIES
        IMPORTED_LOCATION "${CONCORD_LIB_DIR}/libbx.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CONCORD_3RD_DIR};${CONCORD_3RD_DIR}/compat/${BX_COMPAT_PLATFORM}")
# 与预编译库保持一致 (Release, 断言关闭); simd_t.h 需要 SSE4.2; bx 头文件要求 C++20+
target_compile_features(bx INTERFACE cxx_std_20)
target_compile_definitions(bx INTERFACE
        BX_CONFIG_DEBUG=0
        __STDC_LIMIT_MACROS __STDC_FORMAT_MACROS __STDC_CONSTANT_MACROS)
target_compile_options(bx INTERFACE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-msse4.2>)
target_link_libraries(bx INTERFACE psapi)

add_library(bimg STATIC IMPORTED GLOBAL)
set_target_properties(bimg PROPERTIES
        IMPORTED_LOCATION "${CONCORD_LIB_DIR}/libbimg.a"
        INTERFACE_LINK_LIBRARIES bx)

add_library(bimg_decode STATIC IMPORTED GLOBAL)
set_target_properties(bimg_decode PROPERTIES
        IMPORTED_LOCATION "${CONCORD_LIB_DIR}/libbimg_decode.a"
        INTERFACE_LINK_LIBRARIES bimg)

add_library(bgfx STATIC IMPORTED)
set_target_properties(bgfx PROPERTIES
        IMPORTED_LOCATION "${CONCORD_LIB_DIR}/libbgfx.a"
        INTERFACE_LINK_LIBRARIES "bimg_decode;bx")
# bgfx Windows 渲染后端所需系统库
target_link_libraries(bgfx INTERFACE gdi32 user32 uuid)

# Pure cook/id/serialization core shared by CEngine, concord-cook, and tests.
# Lives outside the per-DLL GLOB so the offline cooker does not link CEngine/bgfx.
set(CONCORD_ASSET_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/id/AssetContentHash.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/id/AssetId.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/cook/AssetDependencyGraph.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/cook/CookManifest.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/cook/CookPlanner.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/cook/CookCatalog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/cook/CookSession.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/asset/cook/CookIo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/serialization/BinaryReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/serialization/BinaryWriter.cpp)
add_library(ConcordAssetCore STATIC ${CONCORD_ASSET_CORE_SOURCES})
target_include_directories(ConcordAssetCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(ConcordAssetCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 引擎模块 -> 独立 DLL
#   concord_add_module(<DLL名> DIR <src子目录> [LINK <依赖库...>] [PCH <头文件...>])
#   约定: 模块源码在 src/<DIR>/, 公共头在 include/Concord/
#   src/<DIR> 下没有 .cpp 时自动跳过 (不生成该 DLL)
#   PCH: 该模块常用但很少改动的重头文件 (通常是第三方头), 预编译后可大幅
#        缩短该模块每次增量编译的时间, 见 AGENTS.md "编译速度" 相关约定
define_property(GLOBAL PROPERTY CONCORD_MODULES BRIEF_DOCS "已启用的引擎模块" FULL_DOCS "已启用的引擎模块")

function(concord_set_runtime_output target outputDir)
    set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${outputDir}")
    foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${outputDir}")
    endforeach()
endfunction()

function(concord_add_module NAME)
    cmake_parse_arguments(ARG "" "DIR" "LINK;PCH" ${ARGN})
    file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${ARG_DIR}/*.cpp)
    if(ARG_DIR STREQUAL "engine")
        # Asset core is compiled once into ConcordAssetCore and linked below.
        list(REMOVE_ITEM _srcs ${CONCORD_ASSET_CORE_SOURCES})
    endif()
    if(NOT _srcs)
        message(STATUS "concord: 模块 ${NAME} 无源码, 跳过")
        return()
    endif()

    add_library(${NAME} SHARED ${_srcs})
    # 输出 CEngine.dll 而非 libCEngine.dll
    # Keep each build tree's engine DLLs private. Executable targets copy their
    # complete, toolchain-matched runtime set into their own output directory.
    # This prevents a module-only or test build from replacing bin/*.dll next
    # to an executable produced by a different MinGW installation.
    set_target_properties(${NAME} PROPERTIES PREFIX "")
    concord_set_runtime_output(${NAME} "${CMAKE_CURRENT_BINARY_DIR}/runtime")
    # 导出宏: 见 include/Concord/CExport.h
    string(TOUPPER ${NAME} _upper)
    target_compile_definitions(${NAME} PRIVATE ${_upper}_EXPORTS)
    target_include_directories(${NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(ARG_LINK)
        target_link_libraries(${NAME} PRIVATE ${ARG_LINK})
    endif()
    if(ARG_DIR STREQUAL "engine")
        target_link_libraries(${NAME} PRIVATE ConcordAssetCore)
    endif()
    if(ARG_PCH)
        target_precompile_headers(${NAME} PRIVATE ${ARG_PCH})
    endif()
    set_property(GLOBAL APPEND PROPERTY CONCORD_MODULES ${NAME})
endfunction()

# std::thread / std::this_thread 需要线程库 (MinGW 下即 winpthread)
find_package(Threads REQUIRED)

# 注意: bgfx 为静态库, 只允许链入 CEngine 一个 DLL,
# 其他模块需要渲染时通过 CEngine 暴露的接口访问, 避免多份 bgfx 全局状态
concord_add_module(CEngine DIR engine LINK bgfx SDL3::SDL3 Threads::Threads shell32 PCH <bgfx/bgfx.h> <SDL3/SDL.h>)
concord_add_module(CAudio  DIR audio  LINK Phonon::Phonon SDL3::SDL3 Threads::Threads)
concord_add_module(CGUI    DIR gui LINK CEngine)
concord_add_module(CTime   DIR time   LINK Threads::Threads)
concord_add_module(CSystem DIR system LINK SDL3::SDL3)

# Offline incremental cooker: links only ConcordAssetCore (no bgfx / CEngine).
add_executable(concord-cook tools/cook/main.cpp)
target_link_libraries(concord-cook PRIVATE ConcordAssetCore)
concord_set_runtime_output(concord-cook "${CMAKE_CURRENT_BINARY_DIR}/tools")

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineRegressionTests.cpp")
    add_library(ConcordEventTypeIdentityProbe SHARED
            tests/EventTypeIdentityProbe.cpp)
    target_compile_definitions(ConcordEventTypeIdentityProbe PRIVATE CONCORD_EVENT_PROBE_EXPORTS)
    target_include_directories(ConcordEventTypeIdentityProbe PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(ConcordEventTypeIdentityProbe PRIVATE CEngine)
    set_target_properties(ConcordEventTypeIdentityProbe PROPERTIES PREFIX "")
    concord_set_runtime_output(
            ConcordEventTypeIdentityProbe "${CMAKE_CURRENT_BINARY_DIR}/tests")

    add_executable(ConcordEngineRegressionTests
            tests/EngineRegressionTests.cpp
            tests/AssetImportSecurityTests.cpp
            tests/EventBusRegressionTests.cpp
            tests/ObjectRenderRegressionTests.cpp
            tests/WindowInputRegressionTests.cpp
            tests/InputActionsRegressionTests.cpp
            tests/UiRegressionTests.cpp
            tests/SceneIoGoldenTests.cpp
            tests/sceneIo/SceneIoTestSupport.cpp
            tests/sceneIo/SceneIoV6Tests.cpp
            tests/sceneIo/SceneIoPayloadRoundTripTests.cpp
            tests/sceneIo/SceneIoV7RoundTripTests.cpp
            tests/sceneIo/SceneIoV7ValidationTests.cpp
            tests/SceneLifecycleRegressionTests.cpp
            tests/assetImport/ImportBudgetTests.cpp
            tests/assetImport/ImportPathSandboxTests.cpp
            tests/assetCook/AssetIdentityTests.cpp
            tests/assetCook/CookManifestTests.cpp
            tests/assetCook/CookedMeshTests.cpp
            tests/assetCook/CookedMaterialTests.cpp
            tests/assetCook/CookedRigTests.cpp
            tests/assetCook/CookedTextureTests.cpp
            tests/assetCook/CookedAudioTests.cpp
            tests/assetCook/CookedUiDocumentTests.cpp
            tests/assetCook/CookedSceneTests.cpp
            tests/assetCook/CookedPrefabTests.cpp
            tests/assetCook/CookPlannerTests.cpp
            tests/assetCook/CookSessionTests.cpp
            tests/spatial/SpatialTreeTests.cpp
            tests/spatial/BroadphasePairsTests.cpp
            tests/spatial/PersistentBroadphaseTests.cpp
            tests/spatial/InstanceCullTests.cpp
            tests/spatial/CameraFrustumTests.cpp
            src/engine/spatial/DynamicAabbTree.cpp
            src/engine/spatial/DynamicAabbTreeBalance.cpp
            src/engine/spatial/DynamicAabbTreeQueries.cpp
            src/engine/spatial/Frustum.cpp
            src/engine/spatial/FrustumCuller.cpp
            src/engine/spatial/InstanceCuller.cpp
            src/engine/spatial/ShadowCasterCuller.cpp
            src/engine/render/mesh/MeshBounds.cpp
            src/engine/render/mesh/SkinnedMeshBounds.cpp
            # Water CPU math is exported from CEngine.dll (CENGINE_API), so the
            # tests link it from there rather than re-compiling the sources into
            # the test executable, which would clash with the DLL's exports.
            src/engine/collision/query/RayIntersection.cpp
            src/engine/asset/import/ImportBudget.cpp
            src/engine/asset/import/ImportPathSandbox.cpp
            src/engine/asset/import/ImportModelValidator.cpp
            src/engine/asset/cook/CookedMesh.cpp
            src/engine/asset/cook/CookedMaterial.cpp
            src/engine/asset/cook/CookedSkeleton.cpp
            src/engine/asset/cook/CookedAnimationClip.cpp
            src/engine/asset/cook/CookedTexture.cpp
            src/engine/asset/cook/CookedAudioClip.cpp
            src/engine/asset/cook/CookedUiDocument.cpp
            src/engine/asset/cook/CookedScene.cpp
            src/engine/asset/cook/CookedPrefab.cpp
            src/engine/asset/cook/CookedSceneCodec.cpp
            src/engine/asset/cook/CookedSceneEnvironment.cpp
            src/engine/asset/cook/CookedSceneGraph.cpp
            src/engine/asset/cook/CookedSceneObjectPayload.cpp
            src/engine/asset/cook/CookedScenePayload.cpp
            src/engine/asset/cook/CookedSceneParticle.cpp
            src/engine/asset/import/PlyImporter.cpp
            src/engine/asset/import/gltf/GltfBufferReader.cpp
            src/engine/asset/import/gltf/GltfImageResolver.cpp
            src/engine/asset/import/ply/PlyDataReader.cpp
            src/engine/asset/import/ply/PlyHeader.cpp
            src/engine/asset/import/ply/PlyMeshBuilder.cpp
            src/engine/loop/UpdateDispatcher.cpp
            src/engine/render/batch/RenderBatcher.cpp
            src/engine/render/lighting/ClusteredLightCuller.cpp
            src/engine/render/mesh/Primitives.cpp
            src/engine/render/reflection/ReflectionReceiverSelector.cpp
            src/engine/render/reflection/PlanarReflectionCamera.cpp
            src/engine/render/shadow/ShadowFrustum.cpp
            src/engine/render/frame/WorldSnapshot.cpp
            src/engine/task/JobSystem.cpp
            src/engine/task/TaskGraph.cpp)
    target_include_directories(ConcordEngineRegressionTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CONCORD_3RD_DIR})
    # ConcordAssetCore supplies id/cook-core/serialization; cooked format codecs
    # and importers remain test-local TUs (they depend on mesh/material types).
    target_link_libraries(ConcordEngineRegressionTests PRIVATE
            ConcordAssetCore CEngine bx SDL3::SDL3 Threads::Threads)
    add_dependencies(ConcordEngineRegressionTests ConcordEventTypeIdentityProbe)
    concord_set_runtime_output(
            ConcordEngineRegressionTests "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME ConcordEngineRegressionTests COMMAND ConcordEngineRegressionTests)

    add_executable(ConcordEnvironmentRegressionTests
            tests/EnvironmentRegressionTests.cpp)
    target_compile_definitions(ConcordEnvironmentRegressionTests PRIVATE
            CONCORD_ENVIRONMENT_TEST_MAIN)
    target_include_directories(ConcordEnvironmentRegressionTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(ConcordEnvironmentRegressionTests PRIVATE CEngine)
    concord_set_runtime_output(
            ConcordEnvironmentRegressionTests "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME ConcordEnvironmentRegressionTests COMMAND ConcordEnvironmentRegressionTests)

    add_executable(ConcordSceneCollisionRegressionTests
            tests/SceneCollisionRegressionTests.cpp)
    target_compile_definitions(ConcordSceneCollisionRegressionTests PRIVATE
            CONCORD_SCENE_COLLISION_TEST_MAIN)
    target_include_directories(ConcordSceneCollisionRegressionTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    concord_set_runtime_output(
            ConcordSceneCollisionRegressionTests "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME ConcordSceneCollisionRegressionTests COMMAND ConcordSceneCollisionRegressionTests)

    add_executable(ConcordScreenEffectRegressionTests
            tests/ScreenEffectRegressionTests.cpp
            src/engine/effects/view/ScreenEffectStack.cpp)
    target_compile_definitions(ConcordScreenEffectRegressionTests PRIVATE CENGINE_EXPORTS)
    target_include_directories(ConcordScreenEffectRegressionTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(ConcordScreenEffectRegressionTests PRIVATE Threads::Threads)
    concord_set_runtime_output(
            ConcordScreenEffectRegressionTests "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME ConcordScreenEffectRegressionTests COMMAND ConcordScreenEffectRegressionTests)
endif()

# concord 聚合库: 主程序只需 target_link_libraries(... concord)
add_library(concord INTERFACE)
target_include_directories(concord INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
get_property(_concord_modules GLOBAL PROPERTY CONCORD_MODULES)
if(_concord_modules)
    target_link_libraries(concord INTERFACE ${_concord_modules})
endif()

# 运行时依赖 DLL: 可执行目标调用此函数, 构建后自动复制到输出目录
#   - 第三方: SDL3 / phonon (GPUUtilities、TrueAudioNext 为
#     Steam Audio 可选 GPU 加速组件, 运行时按需加载)
#   - MinGW 运行时: libstdc++ / libgcc / winpthread,
#     保证 bin 下的 exe 脱离 IDE 也能独立运行
function(concord_copy_runtime_dlls target)
    # CMAKE_CURRENT_FUNCTION_LIST_DIR 始终指向本文件所在目录,
    # 保证函数在任意子/父工程作用域调用时路径都正确
    set(_dll_dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/lib)
    set(_dlls
            $<TARGET_RUNTIME_DLLS:${target}>
            "${_dll_dir}/SDL3.dll"
            "${_dll_dir}/phonon.dll"
            "${_dll_dir}/GPUUtilities.dll"
            "${_dll_dir}/TrueAudioNext.dll")

    if(MINGW)
        get_filename_component(_mingw_bin ${CMAKE_CXX_COMPILER} DIRECTORY)
        foreach(_rt libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            if(EXISTS ${_mingw_bin}/${_rt})
                list(APPEND _dlls ${_mingw_bin}/${_rt})
            endif()
        endforeach()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
             ${_dlls}
             $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
            COMMENT "Copying runtime DLLs for ${target}")
endfunction()

if(TARGET ConcordEngineRegressionTests)
    concord_copy_runtime_dlls(ConcordEngineRegressionTests)
endif()

if(TARGET ConcordScreenEffectRegressionTests)
    concord_copy_runtime_dlls(ConcordScreenEffectRegressionTests)
endif()

if(TARGET ConcordEnvironmentRegressionTests)
    concord_copy_runtime_dlls(ConcordEnvironmentRegressionTests)
endif()
