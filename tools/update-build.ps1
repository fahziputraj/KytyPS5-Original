param(
    [switch]$SkipBuild,
    [switch]$PrepareOnly,
    [ValidateSet('Preserve','Performance1080p','Accurate1080p','Accurate1440p')][string]$GraphicsProfile = 'Preserve'
)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskDestination = 'F:\KytyPS5-Build'
$taskBuild = Join-Path $taskRepo '_Build\windows-int19'
$taskStage = Join-Path $taskRepo '_Build\runtime-int19'
$taskCmakeBin = 'C:\Users\fahzi\Documents\Codex\2026-10-15\he\work\build-tools-python\cmake\data\bin'
if (!$SkipBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-windows-local.ps1') -Configure
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; installed binaries were not updated.' }
}
& "$taskCmakeBin\ctest.exe" --test-dir $taskBuild --output-on-failure -R '^(presentation_timing|cpu_placement.*)$'
if ($LASTEXITCODE -ne 0) { throw 'Tests failed; installed binaries were not updated.' }
& "$taskCmakeBin\cmake.exe" --install $taskBuild --prefix $taskStage
if ($LASTEXITCODE -ne 0) { throw 'Staging the runnable build failed.' }
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
foreach ($taskName in @('Start-AstroBot.cmd','Start-AstroBot.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $taskName) -Destination $taskStage -Force
}
Copy-Item -LiteralPath (Join-Path $taskStage 'launcher.exe') -Destination (Join-Path $taskStage 'kyty_launcher.exe') -Force
$taskBundledPatch = 'patches\PPSA21564-01.007-clock30.json'
New-Item -ItemType Directory -Path (Join-Path $taskStage 'patches') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRepo $taskBundledPatch) -Destination (Join-Path $taskStage $taskBundledPatch) -Force
$taskPresetPath = Join-Path $taskDestination 'u59-preset.json'
if (!(Test-Path -LiteralPath $taskPresetPath)) { throw 'The installed int19.1 preset is missing.' }
$taskPreset = Get-Content -LiteralPath $taskPresetPath -Raw | ConvertFrom-Json
foreach ($taskSetting in @{ KYTY_PRESENT_WHEN_READY='1'; KYTY_PRESENT_FPS='0'; KYTY_CPU_HYBRID='on'; KYTY_WRITE_RANGE_STATS='0'; KYTY_DEFER_PROTECTS='0' }.GetEnumerator()) {
    $taskPreset | Add-Member -NotePropertyName $taskSetting.Key -NotePropertyValue $taskSetting.Value -Force
}
$taskPreset | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskStage 'u59-preset.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs\ASTRO-TIMING.md') -Destination (Join-Path $taskStage 'BUILD-NOTES.md') -Force
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
    tests = 'presentation_timing and cpu_placement including hybrid and recorder modes'
    astro_clock = '01.007.000 guarded 30 FPS step; experimental, not variable simulation'
    graphics_profile = $GraphicsProfile
}
$taskManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskStage 'BUILD-INFO.json') -Encoding UTF8
foreach ($taskName in @('kyty_emulator.exe','launcher.exe','kyty_launcher.exe','libwinpthread-1.dll','Qt6Core.dll','Qt6Widgets.dll','platforms\qwindows.dll','u59-preset.json','Start-AstroBot.ps1',$taskBundledPatch)) {
    if (!(Test-Path -LiteralPath (Join-Path $taskStage $taskName))) { throw "Staging is incomplete: $taskName" }
}
if ($PrepareOnly) { Write-Output "Runtime prepared for verification: $taskStage"; return }
$taskActive = Get-Process kyty_emulator,kyty_launcher,launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -and [IO.Path]::GetDirectoryName($_.Path) -eq $taskDestination }
if ($taskActive) { throw 'Close the applications running from F:\KytyPS5-Build before replacing their binaries.' }
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
$taskBackup = Join-Path 'F:\KytyPS5-Build-Backups' ([DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-before-realtime-pacing')
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
$taskManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskDestination 'BUILD-INFO.json') -Encoding UTF8
Write-Output "Latest runnable build: $taskDestination; previous runtime: $taskBackup"
