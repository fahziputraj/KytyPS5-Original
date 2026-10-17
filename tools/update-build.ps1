param(
    [switch]$SkipBuild,
    [switch]$PrepareOnly,
    [switch]$Fsr3Probe,
    [switch]$SkipFsr3GpuTest,
    [switch]$DlssPrototype,
    # Move an installed but unselected Experiments\FSR3 or Experiments\DLSS package into the
    # backup. Without it such a package is retained and reported as retained in BUILD-INFO.json.
    [switch]$RemoveUnselectedPackages,
    # Roll back an update that was interrupted (KYTY-UPDATE-INCOMPLETE.json in the destination).
    [switch]$RestoreIncompleteUpdate,
    [string]$Destination = 'E:\KytyPS5-Build',
    [string]$ToolchainConfig = '',
    [ValidateRange(1,64)][int]$Jobs = 3,
    [ValidateSet('Preserve','Performance1080p','Accurate1080p','Accurate1440p')][string]$GraphicsProfile = 'Preserve',
    [ValidateSet('Preserve','Fixed30','Adaptive')][string]$LauncherAstroClock = 'Preserve'
)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskDestination = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
$taskBuild = Join-Path $taskRepo '_Build\windows-int19'
$taskStage = Join-Path $taskRepo '_Build\runtime-int19'
. (Join-Path $PSScriptRoot 'runtime-install.ps1')
function Assert-KytyRuntimeClosed {
    $taskActive = Get-Process kyty_emulator,kyty_launcher,launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -and [IO.Path]::GetDirectoryName($_.Path) -eq $taskDestination }
    if ($taskActive) { throw "Close the applications running from $taskDestination before replacing their binaries." }
}
if ($RestoreIncompleteUpdate) {
    Assert-KytyRuntimeClosed
    if (Restore-KytyIncompleteInstall -Destination $taskDestination) { Write-Output "Restored the runtime from the interrupted update's backup: $taskDestination" }
    else { Write-Output "No interrupted update was recorded in $taskDestination." }
    return
}
# Fail before the long build when the destination cannot accept this update.
if (Test-Path -LiteralPath (Join-Path $taskDestination $KytyInstallJournalName)) {
    throw "An earlier update of $taskDestination did not finish. Run tools/update-build.ps1 -Destination '$taskDestination' -RestoreIncompleteUpdate first."
}
if (!$PrepareOnly -and $GraphicsProfile -ne 'Preserve') {
    $taskIniPath = Join-Path $taskDestination 'Kyty.ini'
    if (!(Test-Path -LiteralPath $taskIniPath -PathType Leaf)) { throw "GraphicsProfile $GraphicsProfile requires an existing Kyty.ini in $taskDestination." }
    $null = Get-KytyGraphicsProfileIni -Ini ([IO.File]::ReadAllText($taskIniPath)) -GraphicsProfile $GraphicsProfile
}
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskTools = Get-KytyWindowsToolchain -Repository $taskRepo -Configuration $ToolchainConfig
$taskCmakeBin = Split-Path -Parent $taskTools.Cmake
if ($SkipFsr3GpuTest -and !$Fsr3Probe) { throw '-SkipFsr3GpuTest requires -Fsr3Probe.' }
if (!$SkipBuild) {
    $taskBuildOptions = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'build-windows-local.ps1'), '-Configure', '-Destination', $taskDestination, '-Jobs', $Jobs)
    if ($ToolchainConfig) { $taskBuildOptions += @('-ToolchainConfig', $ToolchainConfig) }
    if ($Fsr3Probe) { $taskBuildOptions += '-Fsr3Probe' }
    if ($DlssPrototype) { $taskBuildOptions += '-DlssPrototype' }
    & powershell @taskBuildOptions
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; installed binaries were not updated.' }
}
$env:PATH = "$(Join-Path $taskBuild 'launcher');$($taskTools.QtRoot)\bin;$env:PATH"
$taskTestPattern = '^(presentation_timing|simulation_clock_profile|presentation_config|launcher_presentation_config|controller_settings|temporal_camera|temporal_input|cpu_placement.*|dlss_backend'
if ($DlssPrototype) { $taskTestPattern += '|dlss_ngx_mapping|dlss_ngx_link_no_gpu' }
$taskTestPattern += ')$'
& "$taskCmakeBin\ctest.exe" --test-dir $taskBuild --output-on-failure -R $taskTestPattern
if ($LASTEXITCODE -ne 0) { throw 'Tests failed; installed binaries were not updated.' }
# The installer's preflight, rollback, package and preservation checks on a disposable fixture.
& (Join-Path $PSScriptRoot 'test-runtime-install.ps1')
# The Astro shortcut's title/version/executable guards, in the shell the shortcut uses.
& (Join-Path $PSScriptRoot 'test-astro-launcher.ps1') -Shell (Get-Command powershell.exe).Source

# The stage folder is reused between builds and may hold files of earlier builds or options.
# Only the files listed here are installed, and BUILD-INFO.json describes exactly these.
$taskStageFiles = New-Object System.Collections.Generic.List[string]
function Add-KytyStageFile {
    param([string]$Source, [string]$Relative)
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing runtime file: $Source" }
    $taskTarget = Join-Path $taskStage $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskTarget) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $taskTarget -Force
    $taskStageFiles.Add($Relative)
}
function Add-KytyStageFolder {
    param([string]$Source, [string]$Relative)
    Get-ChildItem -LiteralPath $Source -File -Recurse | ForEach-Object {
        Add-KytyStageFile -Source $_.FullName -Relative (Join-Path $Relative (Get-KytyRelativePath $Source $_.FullName))
    }
}
$taskInstallManifest = Join-Path $taskBuild 'install_manifest.txt'
$taskInstallStarted = [DateTime]::UtcNow.AddSeconds(-2)
Invoke-KytyPgoSource -Repository $taskRepo -Action {
    param($taskSourceLink)
    & "$taskCmakeBin\cmake.exe" --install $taskBuild --prefix $taskStage
    if ($LASTEXITCODE -ne 0) { throw 'Staging the runnable build failed.' }
}
if (!(Test-Path -LiteralPath $taskInstallManifest) -or (Get-Item -LiteralPath $taskInstallManifest).LastWriteTimeUtc -lt $taskInstallStarted) {
    throw 'CMake did not write a current install manifest; the staged file list is unknown.'
}
foreach ($taskLine in Get-Content -LiteralPath $taskInstallManifest) {
    if ($taskLine.Trim()) { $taskStageFiles.Add((Get-KytyRelativePath $taskStage $taskLine.Trim())) }
}
Add-KytyStageFile -Source (Join-Path $taskBuild 'controller_probe.exe') -Relative 'controller_probe.exe'
# Keep the complete Qt runtime beside the newly built launcher.
$taskLauncherRuntime = Join-Path $taskBuild 'launcher'
foreach ($taskName in @('Qt6Core.dll','Qt6Gui.dll','Qt6Network.dll','Qt6Widgets.dll','Qt6Svg.dll','icuuc.dll')) {
    $taskFile = Join-Path $taskLauncherRuntime $taskName
    if (Test-Path -LiteralPath $taskFile) { Add-KytyStageFile -Source $taskFile -Relative $taskName }
}
foreach ($taskName in @('platforms','tls','imageformats','styles','iconengines','networkinformation')) {
    $taskFolder = Join-Path $taskLauncherRuntime $taskName
    if (Test-Path -LiteralPath $taskFolder) { Add-KytyStageFolder -Source $taskFolder -Relative $taskName }
}
foreach ($taskName in @('Start-AstroBot.cmd','Start-AstroBot.ps1','Start-AstroBot-Adaptive.cmd')) {
    Add-KytyStageFile -Source (Join-Path $PSScriptRoot $taskName) -Relative $taskName
}
Add-KytyStageFile -Source (Join-Path $taskStage 'launcher.exe') -Relative 'kyty_launcher.exe'
$taskBundledPatch = 'patches\PPSA21564-01.007-clock30.json'
$taskAdaptivePatch = 'patches\PPSA21564-01.007-clock-adaptive.json'
foreach ($taskName in @($taskBundledPatch, $taskAdaptivePatch)) { Add-KytyStageFile -Source (Join-Path $taskRepo $taskName) -Relative $taskName }
# An installed preset is never rewritten: the stage holds its exact bytes and the installer
# skips it. Only a fresh installation receives the repository preset with these defaults.
$taskPresetPath = Join-Path $taskDestination 'u59-preset.json'
if (Test-Path -LiteralPath $taskPresetPath) {
    Add-KytyStageFile -Source $taskPresetPath -Relative 'u59-preset.json'
} else {
    $taskPreset = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'u59-preset.json') -Raw | ConvertFrom-Json
    foreach ($taskSetting in @{ KYTY_PRESENT_WHEN_READY='1'; KYTY_PRESENT_FPS='0'; KYTY_CPU_HYBRID='on'; KYTY_WRITE_RANGE_STATS='0'; KYTY_DEFER_PROTECTS='0' }.GetEnumerator()) {
        $taskPreset | Add-Member -NotePropertyName $taskSetting.Key -NotePropertyValue $taskSetting.Value -Force
    }
    Write-KytyUtf8File -Path (Join-Path $taskStage 'u59-preset.json') -Text ($taskPreset | ConvertTo-Json)
    $taskStageFiles.Add('u59-preset.json')
}
Add-KytyStageFile -Source (Join-Path $taskRepo 'docs\ASTRO-TIMING.md') -Relative 'BUILD-NOTES.md'
Add-KytyStageFile -Source (Join-Path $taskRepo 'docs\GLOBAL-PACING-FSR-PROPOSAL.md') -Relative 'GLOBAL-PACING-FSR.md'
foreach ($taskName in @('FSR-INTEGRATION-CHECKPOINT.md', 'FSR-DLSS-DEVELOPMENT.md')) { Add-KytyStageFile -Source (Join-Path $taskRepo "docs\$taskName") -Relative $taskName }
Add-KytyStageFile -Source (Join-Path $taskRepo 'LICENSE') -Relative 'LICENSE'
Add-KytyStageFolder -Source (Join-Path $taskRepo 'LICENSES') -Relative 'LICENSES'
# libFont needs these fallback fonts when the build-time source path is absent.
$taskFontRelative = '3rdparty\tracy\profiler\src\font'
Get-ChildItem -LiteralPath (Join-Path $taskRepo $taskFontRelative) -Filter 'Roboto-*.ttf' -File | ForEach-Object {
    Add-KytyStageFile -Source $_.FullName -Relative (Join-Path $taskFontRelative $_.Name)
}
Add-KytyStageFile -Source (Join-Path $taskRepo '3rdparty\SPIRV-Tools\LICENSE') -Relative (Join-Path $taskFontRelative 'LICENSE-Apache-2.0.txt')
$taskSelectedPackages = @()
$taskFsrInfo = [ordered]@{ bundled = $false; in_game_enabled = $false }
if ($Fsr3Probe) {
    $taskSdkPackage = Join-Path $taskRepo '_Build\deps\fidelityfx-1.1.4\sdk-package'
    $taskFsrDll = Join-Path $taskSdkPackage 'PrebuiltSignedDLL\amd_fidelityfx_vk.dll'
    $taskFsrProbe = Join-Path $taskBuild 'fsr3_vulkan_probe.exe'
    if ((Get-FileHash -LiteralPath $taskFsrDll -Algorithm SHA256).Hash -ne 'A1624CC4238FEF046F30C4D80CE3F47BE63FC5F5373F49E3EE9EDB9960F54C78') {
        throw 'The optional FSR3 library does not match the pinned SDK.'
    }
    if ((Get-AuthenticodeSignature -LiteralPath $taskFsrDll).Status -ne 'Valid') {
        throw 'The optional FSR3 library signature is invalid.'
    }
    if (!$SkipFsr3GpuTest) {
        & $taskFsrProbe $taskFsrDll (Join-Path $taskBuild 'fsr3-smoke-generated.ppm')
        if ($LASTEXITCODE -ne 0) { throw 'The optional FSR3 GPU probe failed; installed binaries were not updated.' }
    }
    $taskSelectedPackages += 'Experiments\FSR3'
    foreach ($taskFile in @($taskFsrProbe, $taskFsrDll, (Join-Path $taskSdkPackage 'sdk\LICENSE.txt'), (Join-Path $PSScriptRoot 'Run-FSR3-Test.cmd'))) {
        Add-KytyStageFile -Source $taskFile -Relative (Join-Path 'Experiments\FSR3' (Split-Path -Leaf $taskFile))
    }
    $taskFsrInfo['bundled'] = $true
    $taskFsrInfo['sdk'] = '1.1.4'
    $taskFsrInfo['library_sha256'] = (Get-FileHash -LiteralPath $taskFsrDll -Algorithm SHA256).Hash
    $taskFsrInfo['gpu_probe_status'] = $(if ($SkipFsr3GpuTest) { 'not_run' } else { 'passed' })
    $taskFsrInfo['validation'] = $(if ($SkipFsr3GpuTest) { 'Compiled and bundled only; GPU probe and gameplay validation pending' } else { 'Six offscreen Vulkan interpolation dispatches and generated-frame readback; no guest presentation tested' })
}
$taskDlssInfo = [ordered]@{ prototype_bundled = $false; in_game_enabled = $false; frame_generation_implemented = $false }
if ($DlssPrototype) {
    $taskDlssRoot = Join-Path $taskRepo '_Build\deps\dlss-310.9.16'
    $taskNgxLibrary = Join-Path $taskDlssRoot 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'
    if ((Get-FileHash -LiteralPath $taskNgxLibrary).Hash -ne 'BC43DF9EC032004FDE681D3E3AFD01CDE239E03A0125B822E88674C84287F99C') {
        throw 'The DLSS NGX library does not match the pinned SDK.'
    }
    $taskSelectedPackages += 'Experiments\DLSS'
    foreach ($taskName in @('dlss_backend_tests.exe','dlss_ngx_tests.exe','dlss_ngx_link_check.exe')) {
        Add-KytyStageFile -Source (Join-Path $taskBuild $taskName) -Relative "Experiments\DLSS\$taskName"
    }
    Add-KytyStageFile -Source (Join-Path $PSScriptRoot 'Run-DLSS-Checks.cmd') -Relative 'Experiments\DLSS\Run-DLSS-Checks.cmd'
    Add-KytyStageFile -Source (Join-Path $taskRepo 'docs\DLSS-BACKEND.md') -Relative 'Experiments\DLSS\DLSS-BACKEND.md'
    Add-KytyStageFile -Source (Join-Path $taskDlssRoot 'LICENSE.txt') -Relative 'Experiments\DLSS\LICENSE-NVIDIA-RTX-SDK.txt'
    $taskDlssInfo['prototype_bundled'] = $true
    $taskDlssInfo['sdk'] = '310.9.16'
    $taskDlssInfo['sdk_commit'] = '9981c6350dc58c0ce21f024fd65e31e851c31689'
    $taskDlssInfo['ngx_library_sha256'] = (Get-FileHash -LiteralPath $taskNgxLibrary).Hash
    $taskDlssInfo['validation'] = 'CPU backend and stub NGX mapping tests; real NGX link only'
    $taskDlssInfo['gpu_execution'] = 'not_run'
    $taskDlssInfo['feature_dll_bundled'] = $false
}
$taskManifest = [ordered]@{
    distribution = 'Local build based on Jetsku int19.1'
    source = $taskRepo
    base_commit = '28bc20f146a276479acf42d2e405fb46a4491b8e'
    commit = (git -C $taskRepo rev-parse HEAD)
    branch = (git -C $taskRepo branch --show-current)
    modified = [bool](git -C $taskRepo status --porcelain)
    built_at = [DateTimeOffset]::Now.ToString('o')
    emulator_sha256 = (Get-FileHash -LiteralPath (Join-Path $taskStage 'kyty_emulator.exe') -Algorithm SHA256).Hash
    launcher_sha256 = (Get-FileHash -LiteralPath (Join-Path $taskStage 'launcher.exe') -Algorithm SHA256).Hash
    tests = 'Controller motion/history, presentation timing/configuration, typed simulation profiles, launcher persistence/inheritance, temporal camera/input guards, CPU placement including hybrid and recorder modes; DLSS CPU contracts and optional NGX mapping/link checks; runtime installer preflight/rollback/package/preservation fixtures; Astro shortcut title/version/executable guards'
    global_controls = 'Independent or fixed presentation, FPS limit, CPU scheduling; simulation pace requires a validated engine profile'
    fsr3 = $taskFsrInfo
    dlss = $taskDlssInfo
    astro_clock = '01.007.000 guarded 30 FPS default; optional experimental adaptive 30-60 FPS clock'
    adaptive_clock_default = $false
    graphics_profile = $GraphicsProfile
    launcher_clock_update = $LauncherAstroClock
}
Write-KytyUtf8File -Path (Join-Path $taskStage 'BUILD-INFO.json') -Text ($taskManifest | ConvertTo-Json -Depth 6)
$taskStageFiles.Add('BUILD-INFO.json')
foreach ($taskName in @('kyty_emulator.exe','launcher.exe','kyty_launcher.exe','libwinpthread-1.dll','Qt6Core.dll','Qt6Widgets.dll','platforms\qwindows.dll','u59-preset.json','Start-AstroBot.ps1','Start-AstroBot-Adaptive.cmd',$taskBundledPatch,$taskAdaptivePatch)) {
    if ($taskStageFiles -notcontains $taskName -or !(Test-Path -LiteralPath (Join-Path $taskStage $taskName))) { throw "Staging is incomplete: $taskName" }
}
# The exact installation set, beside (not inside) the reused stage folder.
[IO.File]::WriteAllLines("$taskStage.files.txt", [string[]]($taskStageFiles | Sort-Object -Unique))
if ($PrepareOnly) { Write-Output "Runtime prepared for verification: $taskStage (installation set: $taskStage.files.txt)"; return }
Assert-KytyRuntimeClosed
$taskPlan = New-KytyInstallPlan -Stage $taskStage -Destination $taskDestination -StageFiles $taskStageFiles.ToArray() `
    -SelectedPackages $taskSelectedPackages -RemoveUnselectedPackages:$RemoveUnselectedPackages `
    -GraphicsProfile $GraphicsProfile -LauncherAstroClock $LauncherAstroClock -BundledPatch $taskBundledPatch -AdaptivePatch $taskAdaptivePatch
$taskBackup = Join-Path "$taskDestination-Backups" ([DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-before-realtime-pacing')
Assert-KytyRuntimeClosed
Invoke-KytyInstallPlan -Plan $taskPlan -Backup $taskBackup -Manifest $taskManifest -PackageManifestKeys @{ 'Experiments\FSR3' = 'fsr3'; 'Experiments\DLSS' = 'dlss' }
foreach ($taskPackage in $taskPlan.Packages.GetEnumerator()) { Write-Output "$($taskPackage.Key): $($taskPackage.Value)" }
Write-Output "Latest runnable build: $taskDestination; previous runtime: $taskBackup"
