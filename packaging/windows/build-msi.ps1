# Builds the Windows MSI from an installed tree (plan/features/0031, ADR-0117, ADR-0224).
#
#   packaging/windows/build-msi.ps1 -BuildDir <build tree> -Tree <installed tree> -OutDir <dir>
#
# CPack WIX with the build tree's CPackConfig.cmake, but it packages <installed tree> as it is
# (CPACK_INSTALL_CMAKE_PROJECTS empty, CPACK_INSTALLED_DIRECTORIES) instead of running the install
# scripts again: no compiler, no windeployqt and no dependency's CMake code runs here, only CPack and
# WiX. The release workflow signs hwio-monitor.exe and hwio.exe in the tree first, so the MSI contains
# the signed files; PR CI (package-windows) runs the same script on the unsigned tree.
#
# <installed tree> comes from `cmake --install <build tree> --prefix <absolute dir>`. CPackConfig.cmake
# holds absolute paths of the configure run (sources, packaging/LICENSE.txt in the build tree, the WiX
# template), so the script runs in the same workspace path as the configure run; release.yml uses the
# same runner image for the build and the sign job. Needs CMake >= 3.30 and WiX >= 4 on PATH.
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$Tree,
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = 'Stop'

$config = Join-Path $BuildDir 'CPackConfig.cmake'
if (-not (Test-Path -LiteralPath $config)) { throw "Not found: $config (configure with the windows-package preset)" }
foreach ($exe in 'hwio-monitor.exe', 'hwio.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $Tree $exe))) { throw "Not an installed tree (no $exe): $Tree" }
}

# CMake's cpack next to cmake: Chocolatey's unrelated cpack.exe can come first on PATH.
$cpack = Join-Path (Split-Path -Parent (Get-Command cmake).Source) 'cpack.exe'
$cpackVersion = [version](((& $cpack --version) | Select-Object -First 1) -replace '^cpack version ([0-9]+\.[0-9]+\.[0-9]+).*$', '$1')
if ($cpackVersion -lt [version]'3.30.0') { throw "CPack WIX with WiX >= 4 needs CMake 3.30 or newer, found $cpackVersion ($cpack)" }

$treeDir = (Resolve-Path -LiteralPath $Tree).Path -replace '\\', '/'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outDir = (Resolve-Path -LiteralPath $OutDir).Path -replace '\\', '/'

& $cpack -G WIX --config $config -B $outDir `
    -D 'CPACK_INSTALL_CMAKE_PROJECTS=' `
    -D "CPACK_INSTALLED_DIRECTORIES=$treeDir;."
if ($LASTEXITCODE -ne 0) { throw "cpack failed with exit code $LASTEXITCODE" }

$msi = @(Get-ChildItem -LiteralPath $outDir -Filter '*.msi')
if ($msi.Count -ne 1) { throw "Expected one MSI in ${outDir}, found $($msi.Count)" }
$msi[0].FullName
