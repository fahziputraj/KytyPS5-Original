param(
    [string]$Cmake = 'cmake',
    [string]$Generator = 'Ninja',
    [string]$CxxCompiler = '',
    [string]$CxxCompilerArgument = '',
    [string]$FsrSdkRoot = '',
    [string]$DlssSdkRoot = '',
    [ValidateRange(1, 64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskBuild = Join-Path $taskRepo '_Build\windows-dev'
$taskCmakeCommand = Get-Command $Cmake -ErrorAction SilentlyContinue
$taskCmake = $(if ($taskCmakeCommand) { $taskCmakeCommand.Source } else { $Cmake })
# Reuse a configured portable toolchain without requiring global PATH changes.
if (!$taskCmakeCommand -and $Cmake -eq 'cmake') {
    $taskCache = Join-Path $taskBuild 'CMakeCache.txt'
    if (Test-Path -LiteralPath $taskCache) {
        $taskCachedCmake = Get-Content -LiteralPath $taskCache | Where-Object { $_ -like 'CMAKE_COMMAND:INTERNAL=*' } | Select-Object -First 1
        if ($taskCachedCmake) { $taskCmake = $taskCachedCmake.Substring('CMAKE_COMMAND:INTERNAL='.Length) }
    }
}
if (!(Test-Path -LiteralPath $taskCmake -PathType Leaf)) {
    throw 'CMake was not found. Supply -Cmake with its executable path or use a developer shell.'
}
$taskCtest = Join-Path (Split-Path -Parent $taskCmake) 'ctest.exe'
if (!(Test-Path -LiteralPath $taskCtest)) { throw "CTest is missing beside CMake: $taskCtest" }
$taskOptions = @('-S', (Join-Path $taskRepo 'tests\temporal'), '-B', $taskBuild,
    '-G', $Generator, '-DCMAKE_BUILD_TYPE=Release', "-DKYTY_FSR3_SDK_ROOT=$FsrSdkRoot", "-DKYTY_DLSS_SDK_ROOT=$DlssSdkRoot")
if ($CxxCompiler) { $taskOptions += "-DCMAKE_CXX_COMPILER=$CxxCompiler" }
if ($CxxCompilerArgument) { $taskOptions += "-DCMAKE_CXX_COMPILER_ARG1=$CxxCompilerArgument" }
& $taskCmake @taskOptions
if ($LASTEXITCODE -ne 0) { throw 'Temporal developer-test configure failed.' }
& $taskCmake --build $taskBuild --config Release --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Temporal developer-test build failed.' }
& $taskCtest --test-dir $taskBuild -C Release --output-on-failure --output-junit (Join-Path $taskBuild 'temporal-tests.xml')
if ($LASTEXITCODE -ne 0) { throw 'Temporal developer tests failed.' }
Write-Output 'CPU temporal checks passed. GPU dispatch, gameplay and performance still require runtime validation.'
