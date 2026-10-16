param([string[]]$Targets = @('launcher', 'resource_materialization_tests', 'resource_tracking_tests', 'shader_preload_tests'), [switch]$Configure)
$ErrorActionPreference = 'Stop'
$taskToolRoot = 'C:\Users\fahzi\Documents\Codex\2026-10-15\he\work'
$taskCompiler = Join-Path $taskToolRoot 'toolchain\LLVM\bin'
$taskCmake = Join-Path $taskToolRoot 'build-tools-python\cmake\data\bin\cmake.exe'
$taskMsvc = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$taskSdk = 'C:\Program Files (x86)\Windows Kits\10'
$taskVersion = '10.0.19041.0'
if (!(Test-Path -LiteralPath $taskMsvc)) { throw "Missing MSVC headers: $taskMsvc" }
$env:INCLUDE = "$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\winrt"
$env:LIB = "$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH = "$taskCompiler;$taskToolRoot\toolchain\glslang\bin;$taskSdk\bin\$taskVersion\x64;$env:PATH"
if ($Configure) {
    & $taskCmake -S . -B _Build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
        "-DCMAKE_PREFIX_PATH=$taskToolRoot/Qt/6.10.3/msvc2022_64" `
        -DKYTY_BUILD_LAUNCHER=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON `
        -DTRACY_NO_SYSTEM_TRACING=ON `
        '-DKYTY_LOCAL_MSVC_COMPAT_VERSION=19.40' '-DCMAKE_INSTALL_PREFIX=F:/KytyPS5-Build' `
        -DFETCHCONTENT_SOURCE_DIR_OPUS=C:/kyty-build-fahzi/_deps/opus-src `
        -DFETCHCONTENT_SOURCE_DIR_XBYAK=C:/kyty-build-fahzi/_deps/xbyak-src `
        -DFETCHCONTENT_SOURCE_DIR_ZYDIS=C:/kyty-build-fahzi/_deps/zydis-src `
        -DFETCHCONTENT_SOURCE_DIR_ZSTD=C:/kyty-build-fahzi/_deps/zstd-src `
        -DFETCHCONTENT_SOURCE_DIR_ZARCHIVE_SOURCE=C:/kyty-build-fahzi/_deps/zarchive_source-src
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $taskCmake --build _Build/windows --target @Targets --parallel 10
exit $LASTEXITCODE
