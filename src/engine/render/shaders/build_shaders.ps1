# Regenerates the embedded shader headers under generated/ from the .sc sources.
#
# Concord is **Vulkan-only**: each shader is compiled once to SPIR-V and the
# resulting C array is written to <name>.bin.h (array name <name>_spv) for
# BGFX_EMBEDDED_SHADER_SPIRV. DirectX DXBC/DXIL targets are not built.
#
#   powershell -ExecutionPolicy Bypass -File build_shaders.ps1
#
# Requires shaderc.exe (bgfx tools) and the bgfx shader include dir; override
# their locations with -Shaderc / -Include if they move.
param(
    [string]$Shaderc = "E:\Concord\_downloads\bgfx-tools-build\cmake\bgfx\shaderc.exe",
    [string]$Include = "E:\Concord\_downloads\bgfx.cmake\bgfx\src"
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
# The .sc sources live here under src/, but the embedded C headers are consumed
# through the public include path, so they are emitted under include/.
$genDir = Resolve-Path (Join-Path $here "..\..\..\..\include\engine\render\shaders\generated")
$varying = Join-Path $here "varying.def.sc"
$varyingPresent = Join-Path $here "varying_present.def.sc"
New-Item -ItemType Directory -Force -Path $genDir | Out-Null

# Vulkan SPIR-V only.
$targets = @(
    @{ suffix = "spv"; platform = "linux"; profile = "spirv" }
)

function Build-Shader([string]$file, [string]$type, [string]$varyingDef, [string]$defines = "", [string]$outName = "") {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($file)
    if ([string]::IsNullOrEmpty($outName)) { $outName = $base }
    $src = Join-Path $here $file
    $combined = Join-Path $genDir "$outName.bin.h"
    Remove-Item $combined -ErrorAction SilentlyContinue
    foreach ($t in $targets) {
        $arr = "${outName}_$($t.suffix)"
        $out = Join-Path $genDir "$arr.bin.h"
        $defineArgs = @()
        if (-not [string]::IsNullOrEmpty($defines)) { $defineArgs = @("--define", $defines) }
        if ($type -eq "compute") {
            & $Shaderc -f $src -o $out --bin2c $arr --type $type `
                --platform $t.platform -p $t.profile -O 3 -i $Include @defineArgs
        } else {
            & $Shaderc -f $src -o $out --bin2c $arr --type $type `
                --platform $t.platform -p $t.profile -O 3 -i $Include --varyingdef $varyingDef @defineArgs
        }
        if ($LASTEXITCODE -ne 0) { throw "shaderc failed for $arr" }
        Get-Content $out | Add-Content $combined
        Remove-Item $out
    }
    Write-Host "built $combined (SPIR-V only)"
}

Build-Shader "vs_mesh.sc" "vertex" $varying
Build-Shader "fs_mesh.sc" "fragment" $varying
$varyingParticleBillboard = Join-Path $here "varying_particle_billboard.def.sc"
Build-Shader "vs_particle_billboard.sc" "vertex" $varyingParticleBillboard
Build-Shader "fs_particle_billboard.sc" "fragment" $varyingParticleBillboard
$varyingSkinned = Join-Path $here "varying_skinned.def.sc"
Build-Shader "vs_mesh_skinned.sc" "vertex" $varyingSkinned
Build-Shader "vs_shadow.sc" "vertex" $varying
Build-Shader "vs_shadow_skinned.sc" "vertex" $varyingSkinned
Build-Shader "fs_shadow.sc" "fragment" $varying
Build-Shader "vs_fullscreen.sc" "vertex" $varyingPresent
Build-Shader "fs_fxaa.sc" "fragment" $varyingPresent
$varyingSky = Join-Path $here "varying_sky.def.sc"
Build-Shader "vs_sky.sc" "vertex" $varyingSky
Build-Shader "fs_sky.sc" "fragment" $varyingSky
$varyingVolCloud = Join-Path $here "varying_volcloud.def.sc"
Build-Shader "vs_volcloud.sc" "vertex" $varyingVolCloud
Build-Shader "fs_volcloud.sc" "fragment" $varyingVolCloud
Build-Shader "vs_volcloud_composite.sc" "vertex" $varyingVolCloud
Build-Shader "fs_volcloud_composite.sc" "fragment" $varyingVolCloud
$varyingSmoke = Join-Path $here "varying_smoke.def.sc"
Build-Shader "vs_smoke.sc" "vertex" $varyingSmoke
# Two variants of the same source: SMOKE_MRT=1 (main path, writes the depth
# proxy on a second attachment for the composite's depth-aware upsample) and
# SMOKE_MRT=0 (compose-only path onto a reflection cubemap face, single
# color attachment only).
Build-Shader "fs_smoke_march.sc" "fragment" $varyingSmoke "SMOKE_MRT=1" "fs_smoke_march"
Build-Shader "fs_smoke_march.sc" "fragment" $varyingSmoke "SMOKE_MRT=0" "fs_smoke_march_single"
Build-Shader "fs_smoke_composite.sc" "fragment" $varyingSmoke
Build-Shader "vs_bloom.sc" "vertex" $varyingPresent
Build-Shader "fs_bloom_down.sc" "fragment" $varyingPresent
Build-Shader "fs_bloom_up.sc" "fragment" $varyingPresent
Build-Shader "fs_smaa_edges.sc" "fragment" $varyingPresent
Build-Shader "fs_smaa_weights.sc" "fragment" $varyingPresent
Build-Shader "fs_smaa_blend.sc" "fragment" $varyingPresent
Build-Shader "cs_raytrace.sc" "compute" $varying
Build-Shader "cs_light_cull.sc" "compute" $varying
Build-Shader "fs_rtresolve.sc" "fragment" $varyingPresent
$varyingDebugText = Join-Path $here "varying_debugtext.def.sc"
Build-Shader "vs_debugtext.sc" "vertex" $varyingDebugText
Build-Shader "fs_debugtext.sc" "fragment" $varyingDebugText
