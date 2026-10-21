# Loads the Visual Studio developer environment for the rest of the GitHub Actions job and pins CC/CXX
# to cl. `cmake --preset` does not do this: architecture/toolset strategy "external" is ignored by CMake
# (an IDE loads the environment from those fields before it runs CMake). Left alone, Ninja finds
# C:/mingw64/bin/c++.exe first and the editor build links with MinGW.
$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe was not found."
    exit 1
}
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $install) {
    Write-Error "Visual Studio with the MSVC toolset was not found."
    exit 1
}
$dev = Join-Path $install.Trim() "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $dev)) {
    Write-Error "VsDevCmd.bat was not found at $dev"
    exit 1
}

$dump = & cmd.exe /d /c "call `"$dev`" -no_logo -arch=x64 -host_arch=x64 >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0) {
    Write-Error "VsDevCmd.bat failed (exit $LASTEXITCODE)."
    exit 1
}

$utf8 = New-Object System.Text.UTF8Encoding $false
$marker = "WINDMVC_" + [guid]::NewGuid().ToString("N")
foreach ($line in $dump) {
    if ($line -notmatch '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') {
        continue
    }
    $name = $Matches[1]
    $value = $Matches[2]
    [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    if ($env:GITHUB_ENV) {
        [System.IO.File]::AppendAllText($env:GITHUB_ENV, "$name<<$marker`n$value`n$marker`n", $utf8)
    }
}

foreach ($name in @('CC', 'CXX')) {
    [Environment]::SetEnvironmentVariable($name, 'cl', 'Process')
    if ($env:GITHUB_ENV) {
        [System.IO.File]::AppendAllText($env:GITHUB_ENV, "$name<<$marker`ncl`n$marker`n", $utf8)
    }
}

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    Write-Error "cl.exe is not on PATH after VsDevCmd."
    exit 1
}
if (-not $env:VCToolsInstallDir) {
    Write-Error "VsDevCmd did not set VCToolsInstallDir."
    exit 1
}
Write-Host "MSVC $env:VCToolsInstallDir"
Write-Host "cl $((Get-Command cl).Source)"
