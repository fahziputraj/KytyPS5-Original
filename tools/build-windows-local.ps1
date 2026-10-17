param(
    [string[]]$Targets = @('launcher', 'kyty_emulator', 'cpu_placement_tests', 'presentation_timing_tests', 'simulation_clock_profile_tests', 'presentation_config_tests', 'launcher_presentation_config_tests', 'controller_settings_tests', 'controller_probe', 'temporal_camera_tests', 'temporal_input_tests'),
    [switch]$Configure,
    [switch]$Fsr3Probe,
    [string]$Destination = 'E:\KytyPS5-Build',
    [string]$ToolchainConfig = '',
    [ValidateRange(1,64)][int]$Jobs = 3
)
$ErrorActionPreference = 'Stop'
if ($Fsr3Probe) { $Configure = $true }
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskTools = Get-KytyWindowsToolchain -Repository $taskRepo -Configuration $ToolchainConfig
$taskCompiler = $taskTools.CompilerBin
$taskCmake = $taskTools.Cmake
$taskMsvc = $taskTools.MsvcRoot
$taskSdk = $taskTools.SdkRoot
$taskVersion = $taskTools.SdkVersion
$taskBuild = Join-Path $taskRepo '_Build\windows-int19'
if (!(Test-Path -LiteralPath $taskMsvc)) { throw "Missing MSVC headers: $taskMsvc" }
$env:INCLUDE = "$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\winrt"
$env:LIB = "$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH = "$taskCompiler;$(Split-Path -Parent $taskTools.Glslang);$taskSdk\bin\$taskVersion\x64;$($taskTools.QtRoot)\bin;$env:PATH"
$taskCompilerVersion = & (Join-Path $taskCompiler 'clang-cl.exe') --version | Select-Object -First 1
if ($taskCompilerVersion -notmatch 'clang version 22\.1\.3\b') { throw "The bundled PGO profile requires LLVM 22.1.3: $taskCompilerVersion" }
# PGO hashes this source path into anonymous function names. The helper restores
# the previous junction state, without moving or deleting either checkout.
Invoke-KytyPgoSource -Repository $taskRepo -Action {
    param($taskSourceLink)
    if ($Configure) {
        $taskOptions = @(
            '-S', $taskSourceLink, '-B', $taskBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
            "-DCMAKE_C_COMPILER=$taskCompiler/clang-cl.exe", "-DCMAKE_CXX_COMPILER=$taskCompiler/clang-cl.exe",
            "-DCMAKE_LINKER=$taskCompiler/lld-link.exe", "-DCMAKE_AR=$taskCompiler/llvm-lib.exe",
            "-DCMAKE_PREFIX_PATH=$($taskTools.QtRoot)",
            "-DKYTY_GLSLANG_VALIDATOR=$($taskTools.Glslang)",
            '-DKYTY_BUILD_LAUNCHER=ON', '-DKYTY_EMULATOR_IPO=ON',
            '-DKYTY_PGO_USE=C:/kyty-src19/tools/pgo/u59-int19-sg-1.profdata',
            '-DKYTY_BUILD_ORIGIN=Fork', '-DKYTY_BUILD_REPOSITORY=fahziputraj/KytyPS5-Original',
            '-DTRACY_NO_SYSTEM_TRACING=ON', '-DKYTY_LOCAL_MSVC_COMPAT_VERSION=19.40',
            "-DCMAKE_INSTALL_PREFIX=$Destination"
        )
        if ($taskTools.Ninja) { $taskOptions += "-DCMAKE_MAKE_PROGRAM=$($taskTools.Ninja)" }
        # Use CMake's normal dependency locations in this checkout; do not
        # depend on source directories belonging to a different computer.
        if ($Fsr3Probe) {
            $taskOptions += '-DKYTY_FSR3_SDK_ROOT=' + (Join-Path $taskRepo '_Build\deps\fidelityfx-1.1.4\sdk-package')
        }
        & $taskCmake @taskOptions
        if ($LASTEXITCODE -ne 0) { throw "Configure failed: $LASTEXITCODE" }
    }
    # Windows PowerShell 5 creates a new local variable for += in this scope.
    # Copy the caller's array before appending the optional probe target.
    [string[]]$taskBuildTargets = @($Targets)
    if ($Fsr3Probe) { $taskBuildTargets += 'fsr3_vulkan_probe' }
    & $taskCmake --build $taskBuild --target @taskBuildTargets --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
}
exit 0
