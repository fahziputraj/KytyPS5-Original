param(
    [switch]$SkipBuild,
    [switch]$PrepareOnly,
    [switch]$Fsr3Probe,
    [switch]$SkipFsr3GpuTest,
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
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskTools = Get-KytyWindowsToolchain -Repository $taskRepo -Configuration $ToolchainConfig
$taskCmakeBin = Split-Path -Parent $taskTools.Cmake
if ($SkipFsr3GpuTest -and !$Fsr3Probe) { throw '-SkipFsr3GpuTest requires -Fsr3Probe.' }
if (!$SkipBuild) {
    $taskBuildOptions = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'build-windows-local.ps1'), '-Configure', '-Destination', $taskDestination, '-Jobs', $Jobs)
    if ($ToolchainConfig) { $taskBuildOptions += @('-ToolchainConfig', $ToolchainConfig) }
    if ($Fsr3Probe) { $taskBuildOptions += '-Fsr3Probe' }
    & powershell @taskBuildOptions
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; installed binaries were not updated.' }
}
$env:PATH = "$(Join-Path $taskBuild 'launcher');$($taskTools.QtRoot)\bin;$env:PATH"
& "$taskCmakeBin\ctest.exe" --test-dir $taskBuild --output-on-failure -R '^(presentation_timing|simulation_clock_profile|presentation_config|launcher_presentation_config|controller_settings|temporal_camera|temporal_input|cpu_placement.*)$'
if ($LASTEXITCODE -ne 0) { throw 'Tests failed; installed binaries were not updated.' }
Invoke-KytyPgoSource -Repository $taskRepo -Action {
    param($taskSourceLink)
    & "$taskCmakeBin\cmake.exe" --install $taskBuild --prefix $taskStage
    if ($LASTEXITCODE -ne 0) { throw 'Staging the runnable build failed.' }
}
Copy-Item -LiteralPath (Join-Path $taskBuild 'controller_probe.exe') -Destination $taskStage -Force
# Keep the complete Qt runtime beside the newly built launcher.
$taskLauncherRuntime = Join-Path $taskBuild 'launcher'
foreach ($taskName in @('Qt6Core.dll','Qt6Gui.dll','Qt6Network.dll','Qt6Widgets.dll','Qt6Svg.dll','icuuc.dll')) {
    $taskFile = Join-Path $taskLauncherRuntime $taskName
    if (Test-Path -LiteralPath $taskFile) { Copy-Item -LiteralPath $taskFile -Destination $taskStage -Force }
}
foreach ($taskName in @('platforms','tls','imageformats','styles','iconengines','networkinformation')) {
    $taskFolder = Join-Path $taskLauncherRuntime $taskName
    if (Test-Path -LiteralPath $taskFolder) { Copy-Item -LiteralPath $taskFolder -Destination $taskStage -Recurse -Force }
}
foreach ($taskName in @('Start-AstroBot.cmd','Start-AstroBot.ps1','Start-AstroBot-Adaptive.cmd')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $taskName) -Destination $taskStage -Force
}
Copy-Item -LiteralPath (Join-Path $taskStage 'launcher.exe') -Destination (Join-Path $taskStage 'kyty_launcher.exe') -Force
$taskBundledPatch = 'patches\PPSA21564-01.007-clock30.json'
New-Item -ItemType Directory -Path (Join-Path $taskStage 'patches') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRepo $taskBundledPatch) -Destination (Join-Path $taskStage $taskBundledPatch) -Force
$taskAdaptivePatch = 'patches\PPSA21564-01.007-clock-adaptive.json'
Copy-Item -LiteralPath (Join-Path $taskRepo $taskAdaptivePatch) -Destination (Join-Path $taskStage $taskAdaptivePatch) -Force
$taskPresetPath = Join-Path $taskDestination 'u59-preset.json'
$taskFreshPreset = !(Test-Path -LiteralPath $taskPresetPath)
if ($taskFreshPreset) { $taskPresetPath = Join-Path $PSScriptRoot 'u59-preset.json' }
$taskPreset = Get-Content -LiteralPath $taskPresetPath -Raw | ConvertFrom-Json
foreach ($taskSetting in @{ KYTY_PRESENT_WHEN_READY='1'; KYTY_PRESENT_FPS='0'; KYTY_CPU_HYBRID='on'; KYTY_WRITE_RANGE_STATS='0'; KYTY_DEFER_PROTECTS='0' }.GetEnumerator()) {
    if ($taskFreshPreset) { $taskPreset | Add-Member -NotePropertyName $taskSetting.Key -NotePropertyValue $taskSetting.Value -Force }
}
$taskPreset | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskStage 'u59-preset.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs\ASTRO-TIMING.md') -Destination (Join-Path $taskStage 'BUILD-NOTES.md') -Force
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs\GLOBAL-PACING-FSR-PROPOSAL.md') -Destination (Join-Path $taskStage 'GLOBAL-PACING-FSR.md') -Force
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs\FSR-INTEGRATION-CHECKPOINT.md'),(Join-Path $taskRepo 'docs\FSR-DLSS-DEVELOPMENT.md'),(Join-Path $taskRepo 'LICENSE') -Destination $taskStage -Force
Copy-Item -LiteralPath (Join-Path $taskRepo 'LICENSES') -Destination $taskStage -Recurse -Force
# libFont needs these fallback fonts when the build-time source path is absent.
$taskFontDestination = Join-Path $taskStage '3rdparty\tracy\profiler\src\font'
New-Item -ItemType Directory -Path $taskFontDestination -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $taskRepo '3rdparty\tracy\profiler\src\font') -Filter 'Roboto-*.ttf' -File | Copy-Item -Destination $taskFontDestination -Force
Copy-Item -LiteralPath (Join-Path $taskRepo '3rdparty\SPIRV-Tools\LICENSE') -Destination (Join-Path $taskFontDestination 'LICENSE-Apache-2.0.txt') -Force
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
    $taskFsrDestination = Join-Path $taskStage 'Experiments\FSR3'
    New-Item -ItemType Directory -Path $taskFsrDestination -Force | Out-Null
    Copy-Item -LiteralPath $taskFsrProbe,$taskFsrDll,(Join-Path $taskSdkPackage 'sdk\LICENSE.txt'),(Join-Path $PSScriptRoot 'Run-FSR3-Test.cmd') -Destination $taskFsrDestination -Force
    $taskFsrInfo['bundled'] = $true
    $taskFsrInfo['sdk'] = '1.1.4'
    $taskFsrInfo['library_sha256'] = (Get-FileHash -LiteralPath $taskFsrDll -Algorithm SHA256).Hash
    $taskFsrInfo['gpu_probe_status'] = $(if ($SkipFsr3GpuTest) { 'not_run' } else { 'passed' })
    $taskFsrInfo['validation'] = $(if ($SkipFsr3GpuTest) { 'Compiled and bundled only; GPU probe and gameplay validation pending' } else { 'Six offscreen Vulkan interpolation dispatches and generated-frame readback; no guest presentation tested' })
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
    tests = 'Controller motion/history, presentation timing/configuration, typed simulation profiles, launcher persistence/inheritance, temporal camera/input guards, CPU placement including hybrid and recorder modes'
    global_controls = 'Independent or fixed presentation, FPS limit, CPU scheduling; simulation pace requires a validated engine profile'
    fsr3 = $taskFsrInfo
    astro_clock = '01.007.000 guarded 30 FPS default; optional experimental adaptive 30-60 FPS clock'
    adaptive_clock_default = $false
    graphics_profile = $GraphicsProfile
    launcher_clock_update = $LauncherAstroClock
}
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskStage 'BUILD-INFO.json') -Encoding UTF8
foreach ($taskName in @('kyty_emulator.exe','launcher.exe','kyty_launcher.exe','libwinpthread-1.dll','Qt6Core.dll','Qt6Widgets.dll','platforms\qwindows.dll','u59-preset.json','Start-AstroBot.ps1','Start-AstroBot-Adaptive.cmd',$taskBundledPatch,$taskAdaptivePatch)) {
    if (!(Test-Path -LiteralPath (Join-Path $taskStage $taskName))) { throw "Staging is incomplete: $taskName" }
}
if ($PrepareOnly) { Write-Output "Runtime prepared for verification: $taskStage"; return }
$taskActive = Get-Process kyty_emulator,kyty_launcher,launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -and [IO.Path]::GetDirectoryName($_.Path) -eq $taskDestination }
if ($taskActive) { throw "Close the applications running from $taskDestination before replacing their binaries." }
$taskPatchDestination = Join-Path $taskDestination '_Patches\PPSA21564.json'
$taskPatchDocument = Get-Content -LiteralPath (Join-Path $taskStage $taskBundledPatch) -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath $taskPatchDestination) {
    $taskExisting = Get-Content -LiteralPath $taskPatchDestination -Raw | ConvertFrom-Json
    if ($taskExisting.id -ne $taskPatchDocument.id -or $taskExisting.version -ne $taskPatchDocument.version -or $taskExisting.process -ne $taskPatchDocument.process) {
        throw 'An Astro patch for a different version is installed; refusing to overwrite it.'
    }
    $taskClockMod = $taskPatchDocument.mods[0]
    $taskPreviousClock = @($taskExisting.mods | Where-Object { $_.id -eq $taskClockMod.id })
    if ($taskPreviousClock.Count -eq 1) { $taskClockMod.enabled = $taskPreviousClock[0].enabled }
    $taskExisting.mods = @($taskExisting.mods | Where-Object { $_.id -ne $taskClockMod.id }) + @($taskClockMod)
    $taskExisting | Add-Member -NotePropertyName source_sha256 -NotePropertyValue $taskPatchDocument.source_sha256 -Force
    $taskPatchDocument = $taskExisting
}
if ($LauncherAstroClock -ne 'Preserve') {
    $taskClockDocuments = @(
        (Get-Content -LiteralPath (Join-Path $taskStage $taskBundledPatch) -Raw | ConvertFrom-Json),
        (Get-Content -LiteralPath (Join-Path $taskStage $taskAdaptivePatch) -Raw | ConvertFrom-Json)
    )
    $taskSelectedClock = $(if ($LauncherAstroClock -eq 'Adaptive') { 'codex.astro.clock-adaptive' } else { 'codex.astro.clock30' })
    foreach ($taskClockDocument in $taskClockDocuments) {
        if ($taskClockDocument.id -ne $taskPatchDocument.id -or $taskClockDocument.version -ne $taskPatchDocument.version -or
            $taskClockDocument.process -ne $taskPatchDocument.process -or $taskClockDocument.source_sha256 -ne $taskPatchDocument.source_sha256) {
            throw 'The bundled Astro clock profiles do not match the installed title/version.'
        }
        $taskClockMod = $taskClockDocument.mods[0]
        $taskClockMod.enabled = $taskClockMod.id -eq $taskSelectedClock
        $taskPatchDocument.mods = @($taskPatchDocument.mods | Where-Object { $_.id -ne $taskClockMod.id }) + @($taskClockMod)
    }
}
$taskBackup = Join-Path "$taskDestination-Backups" ([DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-before-realtime-pacing')
New-Item -ItemType Directory -Path $taskBackup | Out-Null
$taskStageRoot = [IO.Path]::GetFullPath($taskStage).TrimEnd('\') + '\'
$taskFiles = Get-ChildItem -LiteralPath $taskStage -File -Recurse
# Only staged runtime files are overwritten. No cache, save or INI files are in this stage.
foreach ($taskFile in $taskFiles) {
    $taskRelative = $taskFile.FullName.Substring($taskStageRoot.Length)
    if ($taskRelative -match '^(_|Kyty\.ini$)') { throw "Unexpected user data in runtime stage: $taskRelative" }
    $taskInstalled = Join-Path $taskDestination $taskRelative
    if (Test-Path -LiteralPath $taskInstalled) {
        $taskSaved = Join-Path $taskBackup $taskRelative
        New-Item -ItemType Directory -Path (Split-Path -Parent $taskSaved) -Force | Out-Null
        Copy-Item -LiteralPath $taskInstalled -Destination $taskSaved -Force
    }
}
Copy-Item -LiteralPath (Join-Path $taskDestination 'Kyty.ini') -Destination $taskBackup -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $taskPatchDestination) {
    New-Item -ItemType Directory -Path (Join-Path $taskBackup '_Patches') -Force | Out-Null
    Copy-Item -LiteralPath $taskPatchDestination -Destination (Join-Path $taskBackup '_Patches\PPSA21564.json')
}
$taskSaveRoot = Join-Path $taskDestination '_SaveData'
$taskSaveHashes = @{}
if (Test-Path -LiteralPath $taskSaveRoot) {
    Get-ChildItem -LiteralPath $taskSaveRoot -File -Recurse | ForEach-Object {
        $taskSaveHashes[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    Copy-Item -LiteralPath $taskSaveRoot -Destination $taskBackup -Recurse
}
foreach ($taskFile in $taskFiles) {
    $taskInstalled = Join-Path $taskDestination $taskFile.FullName.Substring($taskStageRoot.Length)
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskInstalled) -Force | Out-Null
    Copy-Item -LiteralPath $taskFile.FullName -Destination $taskInstalled -Force
}
New-Item -ItemType Directory -Path (Split-Path -Parent $taskPatchDestination) -Force | Out-Null
$taskPatchDocument | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $taskPatchDestination -Encoding UTF8
if ($GraphicsProfile -ne 'Preserve') {
    $taskIniPath = Join-Path $taskDestination 'Kyty.ini'
    $taskIni = Get-Content -LiteralPath $taskIniPath -Raw
    $taskMatch = [regex]::Match($taskIni, '(?ms)^\[GlobalConfiguration\]\r?\n.*?(?=^\[|\z)')
    if (!$taskMatch.Success) { throw 'GlobalConfiguration was not found in Kyty.ini.' }
    $taskSection = $taskMatch.Value
    $taskResolution = $(if ($GraphicsProfile -eq 'Accurate1440p') { 'R2560X1440' } else { 'R1920X1080' })
    $taskAccuracy = $(if ($GraphicsProfile -eq 'Performance1080p') { 'false' } else { 'true' })
    foreach ($taskOption in @{ screen_resolution=$taskResolution; gpu_occlusion_accurate=$taskAccuracy; readback_linear_images=$taskAccuracy; sync_raw_image_buffers=$taskAccuracy; tessellation_enabled='true' }.GetEnumerator()) {
        $taskPattern = '(?m)^' + [regex]::Escape($taskOption.Key) + '=[^\r\n]*'
        $taskLine = $taskOption.Key + '=' + $taskOption.Value
        if ([regex]::IsMatch($taskSection, $taskPattern)) {
            $taskSection = [regex]::Replace($taskSection, $taskPattern, $taskLine)
        } else {
            $taskSection = $taskSection.TrimEnd() + "`r`n$taskLine`r`n`r`n"
        }
    }
    $taskIni = $taskIni.Substring(0, $taskMatch.Index) + $taskSection + $taskIni.Substring($taskMatch.Index + $taskMatch.Length)
    [IO.File]::WriteAllText($taskIniPath, $taskIni, [Text.UTF8Encoding]::new($false))
}
foreach ($taskSave in $taskSaveHashes.GetEnumerator()) {
    if ((Get-FileHash -LiteralPath $taskSave.Key -Algorithm SHA256).Hash -ne $taskSave.Value) { throw "Save changed during deployment: $($taskSave.Key)" }
}
$taskManifest['backup'] = $taskBackup
$taskManifest['save_files_verified_unchanged_at_install'] = $taskSaveHashes.Count
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskDestination 'BUILD-INFO.json') -Encoding UTF8
Write-Output "Latest runnable build: $taskDestination; previous runtime: $taskBackup"
