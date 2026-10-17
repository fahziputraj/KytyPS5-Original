param(
    [string[]]$Targets = @('launcher', 'kyty_emulator', 'cpu_placement_tests', 'presentation_timing_tests', 'simulation_clock_profile_tests', 'presentation_config_tests', 'launcher_presentation_config_tests', 'controller_settings_tests', 'controller_probe', 'temporal_camera_tests'),
    [switch]$Configure,
    [switch]$Fsr3Probe,
    [int]$Jobs = 10
)
$ErrorActionPreference = 'Stop'
if ($Fsr3Probe) { $Configure = $true }
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskToolRoot = 'C:\Users\fahzi\Documents\Codex\2026-10-15\he\work'
$taskCompiler = Join-Path $taskToolRoot 'toolchain\LLVM\bin'
$taskCmake = Join-Path $taskToolRoot 'build-tools-python\cmake\data\bin\cmake.exe'
$taskMsvc = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$taskSdk = 'C:\Program Files (x86)\Windows Kits\10'
$taskVersion = '10.0.19041.0'
$taskBuild = Join-Path $taskRepo '_Build\windows-int19'
$taskSourceLink = 'C:\kyty-src19'
$taskPreviousTarget = $null
$taskLinkChanged = $false
if (!(Test-Path -LiteralPath $taskMsvc)) { throw "Missing MSVC headers: $taskMsvc" }
$env:INCLUDE = "$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\winrt"
$env:LIB = "$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH = "$taskCompiler;$taskToolRoot\toolchain\glslang\bin;$taskSdk\bin\$taskVersion\x64;$env:PATH"
# PGO hashes this source path into anonymous function names. Repoint only the junction
# during this build and restore it afterwards; neither checkout is moved or deleted.
if (Get-Process ninja,clang-cl,lld-link -ErrorAction SilentlyContinue) {
    throw 'Another native build is running; leave the PGO source junction alone.'
}
try {
    if (Test-Path -LiteralPath $taskSourceLink) {
        $taskLink = Get-Item -LiteralPath $taskSourceLink
        if ($taskLink.LinkType -ne 'Junction') { throw "$taskSourceLink must be a directory junction." }
        $taskPreviousTarget = [IO.Path]::GetFullPath([string]$taskLink.Target[0])
        if ($taskPreviousTarget -ne $taskRepo) {
            [IO.Directory]::Delete($taskSourceLink)
            $taskLinkChanged = $true
            New-Item -ItemType Junction -Path $taskSourceLink -Target $taskRepo | Out-Null
        }
    } else {
        $taskLinkChanged = $true
        New-Item -ItemType Junction -Path $taskSourceLink -Target $taskRepo | Out-Null
    }
    if ($Configure) {
        $taskOptions = @(
            '-S', $taskSourceLink, '-B', $taskBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
            "-DCMAKE_C_COMPILER=$taskCompiler/clang-cl.exe", "-DCMAKE_CXX_COMPILER=$taskCompiler/clang-cl.exe",
            "-DCMAKE_LINKER=$taskCompiler/lld-link.exe", "-DCMAKE_AR=$taskCompiler/llvm-lib.exe",
            "-DCMAKE_PREFIX_PATH=$taskToolRoot/Qt/6.10.3/msvc2022_64",
            '-DKYTY_BUILD_LAUNCHER=ON', '-DKYTY_EMULATOR_IPO=ON',
            '-DKYTY_PGO_USE=C:/kyty-src19/tools/pgo/u59-int19-sg-1.profdata',
            '-DKYTY_BUILD_ORIGIN=Fork', '-DKYTY_BUILD_REPOSITORY=fahziputraj/KytyPS5-Original',
            '-DTRACY_NO_SYSTEM_TRACING=ON', '-DKYTY_LOCAL_MSVC_COMPAT_VERSION=19.40',
            '-DCMAKE_INSTALL_PREFIX=F:/KytyPS5-Build',
            '-DFETCHCONTENT_SOURCE_DIR_OPUS=C:/kyty-build-fahzi/_deps/opus-src',
            '-DFETCHCONTENT_SOURCE_DIR_XBYAK=C:/kyty-build-fahzi/_deps/xbyak-src',
            '-DFETCHCONTENT_SOURCE_DIR_ZYDIS=C:/kyty-build-fahzi/_deps/zydis-src',
            '-DFETCHCONTENT_SOURCE_DIR_ZSTD=C:/kyty-build-fahzi/_deps/zstd-src',
            '-DFETCHCONTENT_SOURCE_DIR_ZARCHIVE_SOURCE=C:/kyty-build-fahzi/_deps/zarchive_source-src'
        )
        if ($Fsr3Probe) {
            $taskOptions += '-DKYTY_FSR3_SDK_ROOT=' + (Join-Path $taskRepo '_Build\deps\fidelityfx-1.1.4\sdk-package')
        }
        & $taskCmake @taskOptions
        if ($LASTEXITCODE -ne 0) { throw "Configure failed: $LASTEXITCODE" }
    }
    if ($Fsr3Probe) { $Targets += 'fsr3_vulkan_probe' }
    & $taskCmake --build $taskBuild --target @Targets --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
} finally {
    if ($taskLinkChanged) {
        if (Test-Path -LiteralPath $taskSourceLink) {
            $taskCurrentLink = Get-Item -LiteralPath $taskSourceLink
            if ($taskCurrentLink.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath([string]$taskCurrentLink.Target[0]) -ne $taskRepo) {
                throw 'The PGO source junction changed during the build; refusing to remove it.'
            }
            [IO.Directory]::Delete($taskSourceLink)
        }
        if ($taskPreviousTarget) { New-Item -ItemType Junction -Path $taskSourceLink -Target $taskPreviousTarget | Out-Null }
    }
}
exit 0
