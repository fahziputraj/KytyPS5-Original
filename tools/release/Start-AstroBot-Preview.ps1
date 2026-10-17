param(
    [Parameter(Position=0)][string]$GamePath,
    [ValidateSet('Fixed30','Adaptive','Original')][string]$Mode = 'Fixed30',
    [ValidateSet('on','off','background')][string]$CpuMode = 'on',
    [ValidateRange(-1,15)][int]$Gpu = -1,
    [switch]$LogOutput,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
if (!$GamePath) {
    if ($DryRun) { throw '-GamePath is required for -DryRun.' }
    $GamePath = Read-Host 'Folder containing your ASTRO BOT eboot.bin'
}
$GamePath = $GamePath.Trim().Trim('"')
$taskGame = (Resolve-Path -LiteralPath $GamePath).ProviderPath
$taskEboot = Join-Path $taskGame 'eboot.bin'
if (!(Test-Path -LiteralPath $taskEboot -PathType Leaf)) { throw 'Select the game folder containing eboot.bin.' }
$taskEmulator = Join-Path $PSScriptRoot 'kyty_emulator.exe'
$taskPresetPath = Join-Path $PSScriptRoot 'u59-preset.json'
foreach ($taskFile in @($taskEmulator, $taskPresetPath)) {
    if (!(Test-Path -LiteralPath $taskFile -PathType Leaf)) { throw "Missing release file: $taskFile" }
}
$taskFps = 0
$taskPatchPath = $null
if ($Mode -ne 'Original') {
    $taskPatchName = $(if ($Mode -eq 'Adaptive') { 'PPSA21564-01.007-clock-adaptive.json' } else { 'PPSA21564-01.007-clock30.json' })
    $taskPatchPath = Join-Path $PSScriptRoot ('patches\' + $taskPatchName)
    $taskPatch = Get-Content -LiteralPath $taskPatchPath -Raw | ConvertFrom-Json
    if ($taskPatch.id -ne 'PPSA21564' -or $taskPatch.version -ne '01.007.000' -or $taskPatch.process -ne 'eboot.bin') {
        throw 'The selected clock profile does not match this preview.'
    }
    $taskGameHash = (Get-FileHash -LiteralPath $taskEboot -Algorithm SHA256).Hash
    if ($taskGameHash -ne $taskPatch.source_sha256) {
        throw 'Clock profiles support only the verified PPSA21564 01.007.000 executable. Use -Mode Original for other versions; no clock correction is applied in that mode.'
    }
    $taskFps = $(if ($Mode -eq 'Adaptive') { 60 } else { 30 })
}
$taskPreset = Get-Content -LiteralPath $taskPresetPath -Raw | ConvertFrom-Json
$taskEnvironment = @{}
foreach ($taskSetting in $taskPreset.PSObject.Properties) {
    if ($taskSetting.Name -notmatch '^(KYTY_|TRACY_)') { throw 'Unexpected setting in u59-preset.json.' }
    $taskEnvironment[$taskSetting.Name] = [string]$taskSetting.Value
}
# Match the locally tested performance profile. E-cores remain available for background work.
$taskEnvironment['KYTY_PRESENT_WHEN_READY'] = '1'
$taskEnvironment['KYTY_PRESENT_FPS'] = [string]$taskFps
$taskEnvironment['KYTY_CPU_HYBRID'] = $CpuMode
$taskEnvironment['KYTY_CPU_RESERVE'] = 'cp'
$taskEnvironment['KYTY_WRITE_RANGE_STATS'] = '0'
$taskEnvironment['KYTY_DEFER_PROTECTS'] = '0'
$taskArgs = @('--game', $taskGame, '--redzone', '--amd-cpu',
    '--screen-width', '1920', '--screen-height', '1080', '--present-mode', 'Fifo',
    '--vblank-frequency', '60', '--gpu-occlusion', 'off', '--tessellation',
    '--presentation-clock', 'independent', '--presentation-fps', [string]$taskFps,
    '--shader-log-direction', 'Silent', '--shader-validation', 'false', '--vulkan-validation', 'false',
    '--readback-linear-images', 'false', '--sync-raw-image-buffers', 'false',
    '--printf-direction', $(if ($LogOutput) { 'File' } else { 'Silent' }))
if ($Gpu -ge 0) { $taskArgs += @('--gpu', [string]$Gpu) }
if ($taskPatchPath) { $taskArgs += @('--game-patch', $taskPatchPath) }
if ($LogOutput) { $taskArgs += @('--printf-output-file', '_kyty-preview.txt') }
if ($DryRun) {
    [ordered]@{ executable=$taskEmulator; working_directory=$PSScriptRoot; mode=$Mode; arguments=$taskArgs; environment=$taskEnvironment } | ConvertTo-Json -Depth 5
    return
}
$taskPreviousEnvironment = @{}
Get-ChildItem Env: | Where-Object { $_.Name -match '^(KYTY_|TRACY_)' } | ForEach-Object {
    $taskPreviousEnvironment[$_.Name] = $_.Value
}
Push-Location -LiteralPath $PSScriptRoot
try {
    foreach ($taskName in $taskPreviousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($taskName, $null, 'Process') }
    foreach ($taskSetting in $taskEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($taskSetting.Key, $taskSetting.Value, 'Process') }
    Write-Host "ASTRO BOT preview: $Mode; guest vblank 60 Hz; presentation ceiling $taskFps FPS."
    if ($Mode -eq 'Adaptive') { Write-Host 'Experimental 30-60 FPS clock. Below 30 FPS, slowdown can still occur.' }
    & $taskEmulator @taskArgs
    $taskResult = $LASTEXITCODE
} finally {
    Get-ChildItem Env: | Where-Object { $_.Name -match '^(KYTY_|TRACY_)' } | ForEach-Object {
        [Environment]::SetEnvironmentVariable($_.Name, $null, 'Process')
    }
    foreach ($taskSetting in $taskPreviousEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($taskSetting.Key, $taskSetting.Value, 'Process') }
    Pop-Location
}
exit $taskResult
