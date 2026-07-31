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
    [string]$Shaderc = "",
    [string]$Include = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $here "..\..\..\..\.."))
if ([string]::IsNullOrWhiteSpace($Shaderc)) {
    $Shaderc = if ($env:BGFX_SHADERC) {
        $env:BGFX_SHADERC
    } else {
        Join-Path $repoRoot "_downloads\bgfx-tools-build\cmake\bgfx\shaderc.exe"
    }
}
if ([string]::IsNullOrWhiteSpace($Include)) {
    $Include = if ($env:BGFX_SHADER_INCLUDE) {
        $env:BGFX_SHADER_INCLUDE
    } else {
        Join-Path $repoRoot "_downloads\bgfx.cmake\bgfx\src"
    }
}
if (-not (Test-Path -LiteralPath $Shaderc -PathType Leaf)) {
    throw "bgfx shaderc was not found at '$Shaderc'. Pass -Shaderc or set BGFX_SHADERC."
}
if (-not (Test-Path -LiteralPath $Include -PathType Container)) {
    throw "bgfx shader include directory was not found at '$Include'. Pass -Include or set BGFX_SHADER_INCLUDE."
}
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
    $combinedTemp = "$combined.tmp"
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
        throw "shader source was not found at '$src'"
    }
    Remove-Item -LiteralPath $combinedTemp -ErrorAction SilentlyContinue
    try {
        foreach ($t in $targets) {
            $arr = "${outName}_$($t.suffix)"
            $out = Join-Path $genDir "$arr.bin.h.tmp"
            $defineArgs = @()
            if (-not [string]::IsNullOrEmpty($defines)) { $defineArgs = @("--define", $defines) }
            if ($type -eq "compute") {
                & $Shaderc -f $src -o $out --bin2c $arr --type $type `
                    --platform $t.platform -p $t.profile -O 3 -i $Include -i $here @defineArgs
            } else {
                & $Shaderc -f $src -o $out --bin2c $arr --type $type `
                    --platform $t.platform -p $t.profile -O 3 -i $Include -i $here --varyingdef $varyingDef @defineArgs
            }
            if ($LASTEXITCODE -ne 0) { throw "shaderc failed for $arr" }
            [System.IO.File]::AppendAllText(
                $combinedTemp, [System.IO.File]::ReadAllText($out))
            Remove-Item -LiteralPath $out
        }
        Move-Item -LiteralPath $combinedTemp -Destination $combined -Force
    } finally {
        Remove-Item -LiteralPath $combinedTemp -ErrorAction SilentlyContinue
        Get-ChildItem -LiteralPath $genDir -Filter "${outName}_*.bin.h.tmp" `
            -ErrorAction SilentlyContinue | Remove-Item -Force
    }
    Write-Host "built $combined (SPIR-V only)"
}

# The shader list lives in shaders.manifest.tsv (shared with
# cmake/ConcordShaders.cmake, which rebuilds shaders incrementally as part of
# the normal build). This script remains the manual full-rebuild path.
$manifest = Join-Path $here "shaders.manifest.tsv"
foreach ($line in Get-Content -LiteralPath $manifest) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#")) { continue }
    $f = $line.Split("`t")
    if ($f.Count -lt 5) { throw "malformed manifest line: $line" }
    $src = $f[0]; $type = $f[1]
    $varyingDef = Join-Path $here $f[2]
    $defines = if ($f[3] -eq "-") { "" } else { $f[3] }
    $outName = if ($f[4] -eq "-") { "" } else { $f[4] }
    Build-Shader $src $type $varyingDef $defines $outName
}
