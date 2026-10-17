param(
    [switch]$LogOutput,
    [ValidateRange(0,60)][int]$Fps = 30,
    [ValidateSet('Variable','Fixed')][string]$Timing = 'Variable',
    [ValidateSet('Accurate','Performance')][string]$Graphics = 'Performance',
    [ValidateSet('30','Original','Adaptive')][string]$Clock = '30',
    [ValidateRange(640,7680)][int]$Width = 1920,
    [ValidateRange(480,4320)][int]$Height = 1080,
    [ValidatePattern('^(active|off|[0-9a-f]{32})$')][string]$MotionSource = 'active',
    [ValidateRange(0,1000)][int]$MotionRate = 250,
    # The game folder holding eboot.bin and sce_sys\param.json. Default: the home PC's H: folders.
    [string]$Game = '',
    # The emulator's AMD-instruction compatibility (--amd-cpu): On (the home default), Auto
    # (on unless the host CPU is AMD) or Off. An explicit choice overrides KYTY_AMD_CPU in the preset.
    [ValidateSet('On','Auto','Off')][string]$AmdCpu = 'On',
    # Print the validated launch as JSON instead of starting the emulator.
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
if ($MotionRate -gt 0 -and $MotionRate -lt 60) { throw 'MotionRate must be 0 (raw) or 60-1000 Hz.' }
Set-Location -LiteralPath $PSScriptRoot
$taskPresetPath = Join-Path $PSScriptRoot 'u59-preset.json'
if (!(Test-Path -LiteralPath $taskPresetPath)) { throw 'u59-preset.json is missing.' }
# This script runs in its own PowerShell process (Start-AstroBot.cmd); the variables below
# reach only the emulator started from it.
Get-ChildItem Env: | Where-Object { $_.Name -match '^(KYTY_|TRACY_)' } | ForEach-Object {
    [Environment]::SetEnvironmentVariable($_.Name, $null, 'Process')
}
$taskPreset = Get-Content -LiteralPath $taskPresetPath -Raw | ConvertFrom-Json
foreach ($taskSetting in $taskPreset.PSObject.Properties) {
    [Environment]::SetEnvironmentVariable($taskSetting.Name, [string]$taskSetting.Value, 'Process')
}
$env:KYTY_PRESENT_WHEN_READY = $(if ($Timing -eq 'Variable') { '1' } else { '0' })
$env:KYTY_PRESENT_FPS = [string]$Fps
if ($Game) {
    $taskGame = [IO.Path]::GetFullPath($Game)
} else {
    $taskGame = 'H:\PS5 Games\PPSA21564-app'
    if (!(Test-Path -LiteralPath (Join-Path $taskGame 'eboot.bin'))) {
        $taskGame = 'H:\PS5 Games\PPSA21564 [ 01.007 ]-[DLPSGAME.COM]\PPSA21564-app'
    }
}
if (!(Test-Path -LiteralPath (Join-Path $taskGame 'eboot.bin'))) { throw "The game folder has no eboot.bin: $taskGame. Pass -Game with the folder that contains eboot.bin." }
# The installed title and version as the emulator's patch guard reads them: APP_VER is
# appVersion when present, otherwise contentVersion (loader/systemContent.cpp).
$taskTitle = ''
$taskVersion = ''
$taskParamPath = Join-Path $taskGame 'sce_sys\param.json'
if (Test-Path -LiteralPath $taskParamPath) {
    $taskParam = Get-Content -LiteralPath $taskParamPath -Raw | ConvertFrom-Json
    if ($taskParam.titleId -is [string]) { $taskTitle = $taskParam.titleId }
    if ($taskParam.contentVersion -is [string]) { $taskVersion = $taskParam.contentVersion }
    if ($taskParam.appVersion -is [string]) { $taskVersion = $taskParam.appVersion }
}
$taskPatchPath = Join-Path $PSScriptRoot 'patches\PPSA21564-01.007-clock30.json'
if ($Clock -eq 'Adaptive') {
    if ($Timing -ne 'Variable' -or $Fps -notin @(0,60)) { throw 'The adaptive clock requires Variable timing and a 60 FPS ceiling (-Fps 0 or 60).' }
    $taskPatchPath = Join-Path $PSScriptRoot 'patches\PPSA21564-01.007-clock-adaptive.json'
}
if ($Clock -eq '30') {
    if ($Timing -ne 'Variable' -or $Fps -ne 30) { throw 'The 30 FPS clock patch requires Variable timing and a 30 FPS cap.' }
}
if ($Clock -ne 'Original') {
    if (!(Test-Path -LiteralPath $taskPatchPath)) { throw 'The selected Astro clock patch is missing.' }
    $taskPatch = Get-Content -LiteralPath $taskPatchPath -Raw | ConvertFrom-Json
    # Title, version and the exact executable must all match before any clock patch applies.
    $taskFound = $(if ($taskTitle) { "$taskTitle $taskVersion" } else { 'a game without a readable sce_sys\param.json' })
    $taskUnsupported = "The $Clock clock patch supports only $($taskPatch.id) $($taskPatch.version) with its verified executable; $taskGame is $taskFound. Start it with -Clock Original for the game's own timing."
    if ($taskTitle -ne $taskPatch.id -or $taskVersion -ne $taskPatch.version) { throw $taskUnsupported }
    $taskGameHash = (Get-FileHash -LiteralPath (Join-Path $taskGame 'eboot.bin') -Algorithm SHA256).Hash
    if ($taskGameHash -ne $taskPatch.source_sha256) { throw "$taskUnsupported (eboot.bin SHA-256 $taskGameHash differs)" }
}
$taskAccurate = $Graphics -eq 'Accurate'
$taskArgs = @('--game', $taskGame, '--redzone')
if ($AmdCpu -eq 'On') { $taskArgs += '--amd-cpu' } elseif ($AmdCpu -eq 'Off') { $taskArgs += '--no-amd-cpu' }
if ($PSBoundParameters.ContainsKey('AmdCpu') -and $env:KYTY_AMD_CPU) {
    Write-Warning "Ignoring KYTY_AMD_CPU=$env:KYTY_AMD_CPU from the preset: -AmdCpu $AmdCpu was requested."
    $env:KYTY_AMD_CPU = $null
}
$taskArgs += @('--gpu', '0',
    '--screen-width', [string]$Width, '--screen-height', [string]$Height, '--present-mode', 'Fifo',
    '--vblank-frequency', '60', '--gpu-occlusion', $(if ($taskAccurate) { 'on' } else { 'off' }), '--tessellation',
    '--controller-motion', $MotionSource, '--controller-motion-rate', [string]$MotionRate,
    '--presentation-clock', $(if ($Timing -eq 'Variable') { 'independent' } else { 'fixed' }), '--presentation-fps', [string]$Fps,
    '--shader-log-direction', 'Silent', '--shader-validation', 'false',
    '--vulkan-validation', 'false', '--readback-linear-images', $taskAccurate.ToString().ToLowerInvariant(),
    '--sync-raw-image-buffers', $taskAccurate.ToString().ToLowerInvariant(), '--printf-direction', $(if ($LogOutput) { 'File' } else { 'Silent' }))
if ($Clock -ne 'Original') { $taskArgs += @('--game-patch', $taskPatchPath) }
if ($LogOutput) { $taskArgs += @('--printf-output-file', '_kyty.txt') }
if ($DryRun) {
    [ordered]@{ game = $taskGame; title = $taskTitle; version = $taskVersion; clock = $Clock; amd_cpu = $AmdCpu
        kyty_amd_cpu = $env:KYTY_AMD_CPU; present_fps = $env:KYTY_PRESENT_FPS; arguments = $taskArgs } | ConvertTo-Json -Depth 4
    exit 0
}
& (Join-Path $PSScriptRoot 'kyty_emulator.exe') @taskArgs
exit $LASTEXITCODE
