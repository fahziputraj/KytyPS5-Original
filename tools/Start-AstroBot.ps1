param(
    [switch]$LogOutput,
    [ValidateRange(0,60)][int]$Fps = 30,
    [ValidateSet('Variable','Fixed')][string]$Timing = 'Variable',
    [ValidateSet('Accurate','Performance')][string]$Graphics = 'Performance',
    [ValidateSet('30','Original','Adaptive')][string]$Clock = '30',
    [ValidateRange(640,7680)][int]$Width = 1920,
    [ValidateRange(480,4320)][int]$Height = 1080,
    [ValidatePattern('^(active|off|[0-9a-f]{32})$')][string]$MotionSource = 'active',
    [ValidateRange(0,1000)][int]$MotionRate = 250
)
$ErrorActionPreference = 'Stop'
if ($MotionRate -gt 0 -and $MotionRate -lt 60) { throw 'MotionRate must be 0 (raw) or 60-1000 Hz.' }
Set-Location -LiteralPath $PSScriptRoot
$taskPresetPath = Join-Path $PSScriptRoot 'u59-preset.json'
if (!(Test-Path -LiteralPath $taskPresetPath)) { throw 'u59-preset.json is missing.' }
Get-ChildItem Env: | Where-Object { $_.Name -match '^(KYTY_|TRACY_)' } | ForEach-Object {
    [Environment]::SetEnvironmentVariable($_.Name, $null, 'Process')
}
$taskPreset = Get-Content -LiteralPath $taskPresetPath -Raw | ConvertFrom-Json
foreach ($taskSetting in $taskPreset.PSObject.Properties) {
    [Environment]::SetEnvironmentVariable($taskSetting.Name, [string]$taskSetting.Value, 'Process')
}
$env:KYTY_PRESENT_WHEN_READY = $(if ($Timing -eq 'Variable') { '1' } else { '0' })
$env:KYTY_PRESENT_FPS = [string]$Fps
$taskGame = 'H:\PS5 Games\PPSA21564-app'
if (!(Test-Path -LiteralPath (Join-Path $taskGame 'eboot.bin'))) {
    $taskGame = 'H:\PS5 Games\PPSA21564 [ 01.007 ]-[DLPSGAME.COM]\PPSA21564-app'
}
if (!(Test-Path -LiteralPath (Join-Path $taskGame 'eboot.bin'))) { throw 'Astro Bot game folder is unavailable.' }
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
    $taskGameHash = (Get-FileHash -LiteralPath (Join-Path $taskGame 'eboot.bin') -Algorithm SHA256).Hash
    if ($taskGameHash -ne $taskPatch.source_sha256) { throw 'The Astro clock patch supports only the verified 01.007.000 executable.' }
}
$taskAccurate = $Graphics -eq 'Accurate'
$taskArgs = @('--game', $taskGame, '--redzone', '--amd-cpu', '--gpu', '0',
    '--screen-width', [string]$Width, '--screen-height', [string]$Height, '--present-mode', 'Fifo',
    '--vblank-frequency', '60', '--gpu-occlusion', $(if ($taskAccurate) { 'on' } else { 'off' }), '--tessellation',
    '--controller-motion', $MotionSource, '--controller-motion-rate', [string]$MotionRate,
    '--presentation-clock', $(if ($Timing -eq 'Variable') { 'independent' } else { 'fixed' }), '--presentation-fps', [string]$Fps,
    '--shader-log-direction', 'Silent', '--shader-validation', 'false',
    '--vulkan-validation', 'false', '--readback-linear-images', $taskAccurate.ToString().ToLowerInvariant(),
    '--sync-raw-image-buffers', $taskAccurate.ToString().ToLowerInvariant(), '--printf-direction', $(if ($LogOutput) { 'File' } else { 'Silent' }))
if ($Clock -ne 'Original') { $taskArgs += @('--game-patch', $taskPatchPath) }
if ($LogOutput) { $taskArgs += @('--printf-output-file', '_kyty.txt') }
& (Join-Path $PSScriptRoot 'kyty_emulator.exe') @taskArgs
exit $LASTEXITCODE
