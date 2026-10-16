param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskDestination = 'F:\KytyPS5-Build'
$taskCmakeBin = 'C:\Users\fahzi\Documents\Codex\2026-10-15\he\work\build-tools-python\cmake\data\bin'
Push-Location $taskRepo
try {
    if (!$SkipBuild) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-windows-local.ps1') -Configure
        if ($LASTEXITCODE -ne 0) { throw 'Build failed; installed binaries were not updated.' }
    }
    & "$taskCmakeBin\ctest.exe" --test-dir _Build/windows --output-on-failure -R '^(resource_tracking|resource_materialization|shader_preload)$'
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed; installed binaries were not updated.' }
    $taskActive = Get-Process kyty_emulator,kyty_launcher,launcher,kyty_updater -ErrorAction SilentlyContinue | Where-Object { $_.Path -and [IO.Path]::GetDirectoryName($_.Path) -eq $taskDestination }
    if ($taskActive) { throw 'Close the applications running from F:\KytyPS5-Build before replacing their binaries.' }
    & "$taskCmakeBin\cmake.exe" --install _Build/windows --prefix $taskDestination
    if ($LASTEXITCODE -ne 0) { throw 'Installing the runnable build failed.' }
    $taskFiles = @('kyty_emulator.exe','kyty_launcher.exe','launcher.exe','kyty_updater.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','platforms\qwindows.dll','libwinpthread-1.dll')
    foreach ($taskFile in $taskFiles) {
        if (!(Test-Path -LiteralPath (Join-Path $taskDestination $taskFile))) { throw "Missing runtime file: $taskFile" }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-AstroBot.cmd') -Destination $taskDestination -Force
    Copy-Item -LiteralPath (Join-Path $taskRepo 'docs\LOCAL_WINDOWS_BUILD.md') -Destination (Join-Path $taskDestination 'BUILD-NOTES.md') -Force
    $taskIniPath = Join-Path $taskDestination 'Kyty.ini'
    if (!(Test-Path -LiteralPath $taskIniPath)) {
        $taskIni = [IO.File]::ReadAllText('C:\ProgramData\Kyty\Kyty.ini')
        foreach ($taskPair in @{ 'amd_cpu_enabled'='false'; 'readback_linear_images'='false'; 'sync_raw_image_buffers'='false'; 'profiler_enabled'='false'; 'vulkan_validation_enabled'='false'; 'shader_validation_enabled'='false'; 'present_mode'='Fifo'; 'vblank_frequency'='60' }.GetEnumerator()) {
            $taskIni = [regex]::Replace($taskIni, '(?m)^' + $taskPair.Key + '=.*$', $taskPair.Key + '=' + $taskPair.Value)
        }
        $taskIni = [regex]::Replace($taskIni, '(?m)^game_dirs=.*$', 'game_dirs="H:/PS5 Games/PPSA21564 [ 01.007 ]-[DLPSGAME.COM]"')
        [IO.File]::WriteAllText($taskIniPath, $taskIni, (New-Object Text.UTF8Encoding($false)))
    }
    $taskManifest = [ordered]@{
        source = $taskRepo
        commit = (git rev-parse HEAD)
        branch = (git branch --show-current)
        modified = [bool](git status --porcelain)
        built_at = [DateTimeOffset]::Now.ToString('o')
        emulator_sha256 = (Get-FileHash -LiteralPath (Join-Path $taskDestination 'kyty_emulator.exe') -Algorithm SHA256).Hash
    }
    $taskManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskDestination 'BUILD-INFO.json') -Encoding UTF8
    Write-Output "Latest runnable build: $taskDestination"
} finally {
    Pop-Location
}
