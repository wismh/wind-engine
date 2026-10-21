# Puts Ninja on PATH for the rest of the job. Visual Studio's C++ CMake tools ship a copy that is not on PATH.
$ErrorActionPreference = "Stop"
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    ninja --version
    exit 0
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $install = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if ($install) {
        $bundled = Join-Path $install.Trim() "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
        if (Test-Path (Join-Path $bundled "ninja.exe")) {
            Add-Content -Path $env:GITHUB_ENV -Value "PATH=$bundled;$env:PATH"
            & (Join-Path $bundled "ninja.exe") --version
            exit 0
        }
    }
}
if (Get-Command choco -ErrorAction SilentlyContinue) {
    choco install ninja -y
    ninja --version
    exit 0
}
Write-Error "Ninja was not found, and neither Visual Studio's CMake tools nor Chocolatey could provide it."
exit 1
