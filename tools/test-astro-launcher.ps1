# Start-AstroBot.ps1 guard checks on disposable fixtures under _Build (dry runs; no emulator).
# The game folders are synthetic: a fake eboot.bin and a matching fixture patch hash.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-astro-launcher.ps1
#   pwsh -NoProfile -File tools/test-astro-launcher.ps1
param([string]$Shell = '', [switch]$KeepFixtures)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$Shell) { $Shell = (Get-Process -Id $PID).Path }
$taskRoot = Join-Path $taskRepo ('_Build\test-astro-launcher\run-{0}-{1}' -f $PID, [DateTime]::UtcNow.Ticks)
$taskRuntime = Join-Path $taskRoot 'runtime'
New-Item -ItemType Directory -Path (Join-Path $taskRuntime 'patches') -Force | Out-Null
$script:taskChecks = 0

function Assert-That {
    param([bool]$Condition, [string]$Text)
    $script:taskChecks++
    if (!$Condition) { throw "Astro launcher check failed: $Text" }
}

function New-Game {
    param([string]$Name, [string]$Eboot, $Param)
    $taskGame = Join-Path $taskRoot "games\$Name"
    New-Item -ItemType Directory -Path (Join-Path $taskGame 'sce_sys') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $taskGame 'eboot.bin'), $Eboot)
    if ($null -ne $Param) { [IO.File]::WriteAllText((Join-Path $taskGame 'sce_sys\param.json'), ($Param | ConvertTo-Json)) }
    return $taskGame
}

# Runs the launcher in its own process, as Start-AstroBot.cmd does.
function Invoke-Launcher {
    param([string[]]$Arguments, [hashtable]$Environment = @{})
    $taskSaved = @{}
    foreach ($taskEntry in $Environment.GetEnumerator()) {
        $taskSaved[$taskEntry.Key] = [Environment]::GetEnvironmentVariable($taskEntry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($taskEntry.Key, $taskEntry.Value, 'Process')
    }
    # Windows PowerShell 5.1 turns a native command's stderr into terminating errors under Stop.
    $ErrorActionPreference = 'Continue'
    try {
        $taskOutput = & $Shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $taskRuntime 'Start-AstroBot.ps1') @Arguments -DryRun 2>&1 | Out-String
        $taskCode = $LASTEXITCODE
    } finally {
        foreach ($taskEntry in $taskSaved.GetEnumerator()) { [Environment]::SetEnvironmentVariable($taskEntry.Key, $taskEntry.Value, 'Process') }
    }
    $taskJson = $null
    if ($taskCode -eq 0) { $taskJson = $taskOutput.Substring($taskOutput.IndexOf('{')) | ConvertFrom-Json }
    # Windows PowerShell 5.1 wraps error text at the console width.
    return [pscustomobject]@{ Code = $taskCode; Output = ($taskOutput -replace '\s+', ' '); Launch = $taskJson }
}

Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-AstroBot.ps1') -Destination $taskRuntime
[IO.File]::WriteAllText((Join-Path $taskRuntime 'u59-preset.json'), '{ "KYTY_CPU_HYBRID": "on", "KYTY_PRESENT_FPS": "0" }')
$taskEboot = 'fixture executable 01.007'
$taskHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($taskEboot))).Replace('-', '')
foreach ($taskName in @('PPSA21564-01.007-clock30.json', 'PPSA21564-01.007-clock-adaptive.json')) {
    $taskPatch = Get-Content -LiteralPath (Join-Path $taskRepo "patches\$taskName") -Raw | ConvertFrom-Json
    Assert-That ($taskPatch.id -eq 'PPSA21564' -and $taskPatch.version -eq '01.007.000' -and $taskPatch.source_sha256 -match '^[0-9a-f]{64}$') "the shipped $taskName names its title, version and executable"
    $taskPatch.source_sha256 = $taskHash
    [IO.File]::WriteAllText((Join-Path $taskRuntime "patches\$taskName"), ($taskPatch | ConvertTo-Json -Depth 20))
}
$taskCertified = New-Game 'certified' $taskEboot @{ titleId = 'PPSA21564'; contentVersion = '01.007.000' }
$taskCurrent = New-Game 'office-01.019' 'fixture executable 01.019' @{ titleId = 'PPSA21567'; contentVersion = '01.019.000' }
$taskLookAlike = New-Game 'version-string-only' 'fixture executable 01.019' @{ titleId = 'PPSA21564'; contentVersion = '01.007.000' }
$taskOtherVersion = New-Game 'other-version-same-hash' $taskEboot @{ titleId = 'PPSA21564'; contentVersion = '01.018.000' }
$taskAppVersion = New-Game 'app-version-wins' $taskEboot @{ titleId = 'PPSA21564'; contentVersion = '01.007.000'; appVersion = '01.019.000' }
$taskOtherTitle = New-Game 'other-title-same-hash' $taskEboot @{ titleId = 'PPSA21567'; contentVersion = '01.007.000' }
$taskNoParam = New-Game 'no-param' $taskEboot $null

# The certified fixture gets the fixed-30 profile with the home default CPU mode.
$taskRun = Invoke-Launcher @('-Game', $taskCertified)
Assert-That ($taskRun.Code -eq 0) "the certified game launches ($($taskRun.Output))"
$taskArgs = @($taskRun.Launch.arguments)
Assert-That ($taskArgs -contains '--amd-cpu' -and $taskArgs -notcontains '--no-amd-cpu') 'the default keeps --amd-cpu'
Assert-That ($taskArgs[[array]::IndexOf($taskArgs, '--game-patch') + 1] -like '*PPSA21564-01.007-clock30.json') 'the default applies the fixed 30 FPS clock'
Assert-That ($taskArgs[[array]::IndexOf($taskArgs, '--vblank-frequency') + 1] -eq '60' -and $taskArgs[[array]::IndexOf($taskArgs, '--presentation-fps') + 1] -eq '30') 'guest vblank stays 60 with a 30 FPS presentation cap'
Assert-That ($taskArgs[[array]::IndexOf($taskArgs, '--gpu-occlusion') + 1] -eq 'off' -and $taskArgs[[array]::IndexOf($taskArgs, '--screen-width') + 1] -eq '1920') 'the default is Performance1080p'
Assert-That ($taskRun.Launch.present_fps -eq '30') 'the preset FPS is overridden for the child only'

# Unsupported games never receive a clock patch.
foreach ($taskCase in @(@($taskCurrent, 'PPSA21567 01.019.000'), @($taskLookAlike, 'eboot.bin SHA-256'), @($taskOtherVersion, 'PPSA21564 01.018.000'),
    @($taskAppVersion, 'PPSA21564 01.019.000'), @($taskOtherTitle, 'PPSA21567 01.007.000'), @($taskNoParam, 'without a readable'))) {
    foreach ($taskClock in @(@(), @('-Clock', 'Adaptive', '-Fps', '0'))) {
        $taskRun = Invoke-Launcher (@('-Game', $taskCase[0]) + $taskClock)
        Assert-That ($taskRun.Code -ne 0 -and $taskRun.Output -match [regex]::Escape($taskCase[1]) -and $taskRun.Output -match '-Clock Original') "$(Split-Path -Leaf $taskCase[0]) $($taskClock -join ' ') is refused with a diagnostic"
    }
}
$taskRun = Invoke-Launcher @('-Game', $taskCurrent, '-Clock', 'Original', '-AmdCpu', 'Auto')
Assert-That ($taskRun.Code -eq 0 -and @($taskRun.Launch.arguments) -notcontains '--game-patch') 'an unsupported game starts with its own timing and no patch'
Assert-That ($taskRun.Launch.title -eq 'PPSA21567' -and $taskRun.Launch.version -eq '01.019.000') 'the launch reports the actual title and version'
Assert-That (@($taskRun.Launch.arguments) -notcontains '--amd-cpu' -and @($taskRun.Launch.arguments) -notcontains '--no-amd-cpu') '-AmdCpu Auto leaves the choice to the emulator'
$taskRun = Invoke-Launcher @('-Game', $taskCurrent, '-Clock', 'Original', '-AmdCpu', 'Off')
Assert-That ($taskRun.Code -eq 0 -and @($taskRun.Launch.arguments) -contains '--no-amd-cpu') '-AmdCpu Off passes --no-amd-cpu'

# The adaptive experiment stays a separate, explicit option on the certified executable.
$taskRun = Invoke-Launcher @('-Game', $taskCertified, '-Clock', 'Adaptive', '-Fps', '0')
Assert-That ($taskRun.Code -eq 0 -and $taskRun.Launch.arguments[[array]::IndexOf(@($taskRun.Launch.arguments), '--game-patch') + 1] -like '*clock-adaptive.json') 'the adaptive clock is opt-in'
$taskRun = Invoke-Launcher @('-Game', $taskCertified, '-Fps', '60')
Assert-That ($taskRun.Code -ne 0 -and $taskRun.Output -match '30 FPS cap') 'the fixed clock refuses a different cap'

# A KYTY_AMD_CPU preset entry still wins unless -AmdCpu is explicit.
[IO.File]::WriteAllText((Join-Path $taskRuntime 'u59-preset.json'), '{ "KYTY_AMD_CPU": "0" }')
$taskRun = Invoke-Launcher @('-Game', $taskCertified)
Assert-That ($taskRun.Code -eq 0 -and $taskRun.Launch.kyty_amd_cpu -eq '0') 'an implicit default keeps the preset CPU override'
$taskRun = Invoke-Launcher @('-Game', $taskCertified, '-AmdCpu', 'Auto')
Assert-That ($taskRun.Code -eq 0 -and $null -eq $taskRun.Launch.kyty_amd_cpu -and $taskRun.Output -match 'Ignoring KYTY_AMD_CPU') 'an explicit -AmdCpu replaces the preset override'

# Variables from the caller do not leak into the launch.
$taskRun = Invoke-Launcher @('-Game', $taskCertified) @{ KYTY_CPU_RESERVE = 'cp+recorder'; KYTY_PRESENT_FPS = '144' }
Assert-That ($taskRun.Code -eq 0 -and $taskRun.Launch.present_fps -eq '30') 'caller KYTY_* variables are replaced by the preset and options'
$taskRun = Invoke-Launcher @('-Game', (Join-Path $taskRoot 'games\missing'))
Assert-That ($taskRun.Code -ne 0 -and $taskRun.Output -match 'no eboot.bin') 'a missing game folder is refused'

Write-Output ("Astro launcher checks: {0} passed with {1}" -f $script:taskChecks, $Shell)
if (!$KeepFixtures) { Remove-Item -LiteralPath $taskRoot -Recurse -Force }
