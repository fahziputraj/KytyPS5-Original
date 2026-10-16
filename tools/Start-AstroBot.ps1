param([switch]$LogOutput)
$ErrorActionPreference = 'Stop'
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
$taskGame = 'H:\PS5 Games\PPSA21564-app'
if (!(Test-Path -LiteralPath (Join-Path $taskGame 'eboot.bin'))) {
    $taskGame = 'H:\PS5 Games\PPSA21564 [ 01.007 ]-[DLPSGAME.COM]\PPSA21564-app'
}
if (!(Test-Path -LiteralPath (Join-Path $taskGame 'eboot.bin'))) { throw 'Astro Bot game folder is unavailable.' }
$taskArgs = @('--game', $taskGame, '--redzone', '--amd-cpu', '--gpu', '0',
    '--screen-width', '1920', '--screen-height', '1080', '--present-mode', 'Fifo',
    '--vblank-frequency', '60', '--gpu-occlusion', 'off', '--tessellation',
    '--shader-log-direction', 'Silent', '--shader-validation', 'false',
    '--vulkan-validation', 'false', '--readback-linear-images', 'false',
    '--sync-raw-image-buffers', 'false', '--printf-direction', $(if ($LogOutput) { 'File' } else { 'Silent' }))
if ($LogOutput) { $taskArgs += @('--printf-output-file', '_kyty.txt') }
& (Join-Path $PSScriptRoot 'kyty_emulator.exe') @taskArgs
exit $LASTEXITCODE
