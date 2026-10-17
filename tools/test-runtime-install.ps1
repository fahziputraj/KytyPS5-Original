# Runtime installer checks (tools/runtime-install.ps1) on disposable fixtures under _Build.
# Never touches a real runtime. Run with Windows PowerShell 5.1 and PowerShell 7:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-runtime-install.ps1
#   pwsh -NoProfile -File tools/test-runtime-install.ps1
param([switch]$KeepFixtures)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'runtime-install.ps1')
$taskRoot = Join-Path $taskRepo ('_Build\test-runtime-install\run-{0}-{1}' -f $PID, [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Path $taskRoot -Force | Out-Null
$script:taskChecks = 0
$script:taskFixture = 0
$taskBundled = 'patches\PPSA21564-01.007-clock30.json'
$taskAdaptive = 'patches\PPSA21564-01.007-clock-adaptive.json'

function Assert-That {
    param([bool]$Condition, [string]$Text)
    $script:taskChecks++
    if (!$Condition) { throw "Runtime installer check failed: $Text" }
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Pattern, [string]$Text)
    $taskMessage = $null
    try { & $Action } catch { $taskMessage = $_.Exception.Message }
    Assert-That ($null -ne $taskMessage -and $taskMessage -match $Pattern) "$Text (got: $taskMessage)"
}

function Write-Bytes {
    param([string]$Path, [byte[]]$Bytes)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllBytes($Path, $Bytes)
}

function Write-Text {
    param([string]$Path, [string]$Text, [switch]$Bom)
    $taskBytes = [Text.Encoding]::UTF8.GetBytes($Text)
    if ($Bom) { $taskBytes = [byte[]](0xEF, 0xBB, 0xBF) + $taskBytes }
    Write-Bytes $Path $taskBytes
}

function Get-TreeHashes {
    param([string]$Root)
    $taskMap = @{}
    if (Test-Path -LiteralPath $Root) {
        Get-ChildItem -LiteralPath $Root -File -Recurse -Force | ForEach-Object {
            $taskMap[(Get-KytyRelativePath $Root $_.FullName)] = Get-KytySha256 $_.FullName
        }
    }
    return $taskMap
}

function Test-SameTree {
    param([hashtable]$Before, [hashtable]$After)
    if ($Before.Count -ne $After.Count) { return $false }
    foreach ($taskEntry in $Before.GetEnumerator()) {
        if ($After[$taskEntry.Key] -ne $taskEntry.Value) { return $false }
    }
    return $true
}

# A staged runtime and an installed runtime holding user data.
function New-Fixture {
    param([switch]$EmptyDestination, [switch]$Ini, [switch]$BadIni)
    $script:taskFixture++
    $taskBase = Join-Path $taskRoot ("case{0:D2}" -f $script:taskFixture)
    $taskStage = Join-Path $taskBase 'stage'
    $taskDest = Join-Path $taskBase 'runtime'
    New-Item -ItemType Directory -Path $taskStage, $taskDest -Force | Out-Null
    $taskFiles = @('kyty_emulator.exe', 'launcher.exe', 'kyty_launcher.exe', 'platforms\qwindows.dll', 'BUILD-INFO.json',
        'Experiments\FSR3\fsr3_vulkan_probe.exe', 'Experiments\FSR3\amd_fidelityfx_vk.dll', 'u59-preset.json', $taskBundled, $taskAdaptive)
    $taskSeed = 0
    foreach ($taskName in $taskFiles) {
        $taskSeed++
        Write-Text (Join-Path $taskStage $taskName) "new build file $taskName $taskSeed"
    }
    Copy-Item -LiteralPath (Join-Path $taskRepo $taskBundled) -Destination (Join-Path $taskStage $taskBundled) -Force
    Copy-Item -LiteralPath (Join-Path $taskRepo $taskAdaptive) -Destination (Join-Path $taskStage $taskAdaptive) -Force
    Write-Text (Join-Path $taskStage 'Experiments\DLSS\dlss_backend_tests.exe') 'stale DLSS file left in the reused stage'
    if (!$EmptyDestination) {
        foreach ($taskName in @('kyty_emulator.exe', 'launcher.exe', 'kyty_launcher.exe', 'platforms\qwindows.dll', 'Experiments\FSR3\amd_fidelityfx_vk.dll')) {
            Write-Text (Join-Path $taskDest $taskName) "previous runtime $taskName"
        }
        Write-Text (Join-Path $taskDest 'Experiments\FSR3\generated-test.ppm') 'probe output written by the user'
        Write-Text (Join-Path $taskDest 'Experiments\DLSS\dlss_backend_tests.exe') 'previous DLSS backend tests'
        Write-Text (Join-Path $taskDest 'Experiments\DLSS\Run-DLSS-Checks.cmd') 'previous DLSS runner'
        Write-Text (Join-Path $taskDest 'BUILD-INFO.json') '{ "commit": "1111111111111111111111111111111111111111", "fsr3": { "bundled": true } }'
        # A hand-edited preset: BOM, nested objects, arrays, unknown keys, odd spacing, no final newline.
        Write-Text (Join-Path $taskDest 'u59-preset.json') ("{`r`n`t""KYTY_CPU_HYBRID"" :  ""on"",`n  ""KYTY_PRESENT_FPS"":""30"", ""x-owner-notes"": { ""nested"": [1, 2, {""deep"": ""caf" + [char]0x00E9 + """}], ""empty"": {} },`r`n  ""KYTY_UNKNOWN_FUTURE"": ""keep me""  }") -Bom
        Write-Bytes (Join-Path $taskDest '_SaveData\PPSA21564\SAVEDATA00\sdimg_save.bin') ([byte[]](0..255))
        Write-Text (Join-Path $taskDest '_PipelineCache\PPSA21564.pipelines.journal') 'pipeline cache'
        Write-Text (Join-Path $taskDest '_Trophies\PPSA21564\trophy.dat') 'trophies'
        # The installed patch: the bundled clock mod plus an owner mod, compact formatting.
        $taskPatch = Get-Content -LiteralPath (Join-Path $taskRepo $taskBundled) -Raw | ConvertFrom-Json
        $taskOwnerMod = [pscustomobject]@{ name = 'Owner mod'; enabled = $false; id = 'owner.mod'; memory = @() }
        $taskPatch.mods = @($taskOwnerMod) + @($taskPatch.mods)
        Write-Text (Join-Path $taskDest '_Patches\PPSA21564.json') ($taskPatch | ConvertTo-Json -Depth 20 -Compress)
    }
    if ($Ini) {
        Write-Text (Join-Path $taskDest 'Kyty.ini') "[Launcher]`r`ngame_dir=F:/PS5 GAMES`r`n`r`n[GlobalConfiguration]`r`nscreen_resolution=R1280X720`r`ngpu_occlusion_accurate=true`r`nowner_value=keep`r`n`r`n[GameConfigurations]`r`nPPSA21564\screen_resolution=R2560X1440`r`n"
    } elseif ($BadIni) {
        Write-Text (Join-Path $taskDest 'Kyty.ini') "[Launcher]`r`ngame_dir=F:/PS5 GAMES`r`n"
    }
    $taskStageFiles = $taskFiles
    return [pscustomobject]@{ Base = $taskBase; Stage = $taskStage; Destination = $taskDest; StageFiles = $taskStageFiles; Backup = (Join-Path $taskBase 'backup') }
}

function New-Plan {
    param($Fixture, [string[]]$Packages = @('Experiments\FSR3'), [string[]]$StageFiles = $null, [switch]$Remove,
        [string]$GraphicsProfile = 'Preserve', [string]$Clock = 'Preserve')
    if ($null -eq $StageFiles) { $StageFiles = $Fixture.StageFiles }
    return New-KytyInstallPlan -Stage $Fixture.Stage -Destination $Fixture.Destination -StageFiles $StageFiles -SelectedPackages $Packages `
        -RemoveUnselectedPackages:$Remove -GraphicsProfile $GraphicsProfile -LauncherAstroClock $Clock -BundledPatch $taskBundled -AdaptivePatch $taskAdaptive
}

function New-Manifest {
    return [ordered]@{ commit = '2222222222222222222222222222222222222222'; fsr3 = [ordered]@{ bundled = $true }; dlss = [ordered]@{ prototype_bundled = $false } }
}

function Install-Fixture {
    param($Fixture, $Plan, [hashtable]$Fault = @{}, $Manifest = (New-Manifest))
    Invoke-KytyInstallPlan -Plan $Plan -Backup $Fixture.Backup -Manifest $Manifest -FaultInjection $Fault `
        -PackageManifestKeys @{ 'Experiments\FSR3' = 'fsr3'; 'Experiments\DLSS' = 'dlss' }
    return $Manifest
}

# 1. Preflight: every failure happens before the destination changes.
$taskCase = New-Fixture
$taskBefore = Get-TreeHashes $taskCase.Destination
Assert-Throws { New-Plan $taskCase -GraphicsProfile Performance1080p } 'requires an existing Kyty.ini' 'a graphics profile without Kyty.ini fails in preflight'
$taskBad = New-Fixture -BadIni
$taskBadBefore = Get-TreeHashes $taskBad.Destination
Assert-Throws { New-Plan $taskBad -GraphicsProfile Accurate1080p } 'GlobalConfiguration was not found' 'a malformed Kyty.ini fails in preflight'
Assert-That (Test-SameTree $taskBadBefore (Get-TreeHashes $taskBad.Destination)) 'a malformed INI leaves the runtime untouched'
Assert-Throws { New-Plan $taskCase -StageFiles ($taskCase.StageFiles + 'Experiments\DLSS\dlss_backend_tests.exe') } 'unselected package' 'a stale unselected package file in the stage is refused'
Write-Text (Join-Path $taskCase.Stage '_SaveData\x.bin') 'x'
Assert-Throws { New-Plan $taskCase -StageFiles ($taskCase.StageFiles + '_SaveData\x.bin') } 'Unexpected user data' 'user data in the stage is refused'
Write-Text (Join-Path $taskCase.Stage 'Kyty.ini') 'x'
Assert-Throws { New-Plan $taskCase -StageFiles ($taskCase.StageFiles + 'Kyty.ini') } 'Unexpected user data' 'an INI in the stage is refused'
Assert-Throws { New-Plan $taskCase -StageFiles ($taskCase.StageFiles + 'missing.dll') } 'Staged file is missing' 'a missing staged file is refused'
Assert-Throws { New-Plan $taskCase -Packages @('Experiments\Other') } 'Unknown optional package' 'an unknown package is refused'
$taskOther = Get-Content -LiteralPath (Join-Path $taskCase.Destination '_Patches\PPSA21564.json') -Raw
Write-Text (Join-Path $taskCase.Destination '_Patches\PPSA21564.json') ($taskOther -replace '01\.007\.000', '01.019.000')
$taskBefore = Get-TreeHashes $taskCase.Destination
Assert-Throws { New-Plan $taskCase } 'different version' 'a patch for another game version is never overwritten'
Assert-That (Test-SameTree $taskBefore (Get-TreeHashes $taskCase.Destination)) 'preflight failures leave every runtime file untouched'
Assert-That (!(Test-Path -LiteralPath $taskCase.Backup)) 'preflight failures create no backup'

# 2. A normal update: selected package updated, unselected retained, user data byte-identical.
$taskCase = New-Fixture -Ini
$taskBefore = Get-TreeHashes $taskCase.Destination
$taskPlan = New-Plan $taskCase
Assert-That ($taskPlan.Writes.Count -eq 0) 'a semantically unchanged patch and preserved INI are not rewritten'
$taskManifest = Install-Fixture $taskCase $taskPlan
$taskAfter = Get-TreeHashes $taskCase.Destination
foreach ($taskName in $taskCase.StageFiles) {
    # The preset is preserved; BUILD-INFO.json is replaced by the install manifest.
    if ($taskName -eq 'u59-preset.json' -or $taskName -eq 'BUILD-INFO.json') { continue }
    Assert-That ($taskAfter[$taskName] -eq (Get-KytySha256 (Join-Path $taskCase.Stage $taskName))) "installed $taskName matches the stage"
}
foreach ($taskName in @('u59-preset.json', 'Kyty.ini', '_Patches\PPSA21564.json', '_SaveData\PPSA21564\SAVEDATA00\sdimg_save.bin',
    '_PipelineCache\PPSA21564.pipelines.journal', '_Trophies\PPSA21564\trophy.dat', 'Experiments\FSR3\generated-test.ppm',
    'Experiments\DLSS\dlss_backend_tests.exe', 'Experiments\DLSS\Run-DLSS-Checks.cmd')) {
    Assert-That ($taskAfter[$taskName] -eq $taskBefore[$taskName]) "$taskName is byte-identical after the update"
}
Assert-That (!(Test-Path -LiteralPath (Join-Path $taskCase.Destination $KytyInstallJournalName))) 'a finished update leaves no journal'
$taskInfo = Get-Content -LiteralPath (Join-Path $taskCase.Destination 'BUILD-INFO.json') -Raw | ConvertFrom-Json
$taskFsr = $taskInfo.packages.'Experiments\FSR3'
$taskDlss = $taskInfo.packages.'Experiments\DLSS'
Assert-That ($taskFsr.installed_state -eq 'updated' -and $taskInfo.fsr3.installed_state -eq 'updated') 'the selected package is reported updated'
Assert-That ($taskFsr.installed_files.'Experiments\FSR3\amd_fidelityfx_vk.dll' -eq (Get-KytySha256 (Join-Path $taskCase.Stage 'Experiments\FSR3\amd_fidelityfx_vk.dll'))) 'the manifest lists the updated package file hash'
Assert-That ($null -ne $taskFsr.other_files_retained.'Experiments\FSR3\generated-test.ppm') 'user files in an updated package are retained and listed'
Assert-That ($taskDlss.installed_state -eq 'retained' -and $taskInfo.dlss.installed_state -eq 'retained') 'an unselected installed package is reported retained'
Assert-That ($taskDlss.retained_from_commit -eq '1111111111111111111111111111111111111111') 'a retained package names the build it came from'
Assert-That (@($taskDlss.installed_files.PSObject.Properties).Count -eq 2) 'the retained package lists exactly its installed files'
Assert-That ($taskInfo.save_files_verified_unchanged_at_install -eq 1 -and $taskInfo.user_data_files_verified_unchanged -eq 3) 'the manifest counts verified saves and user data'
Assert-That (@($taskInfo.settings_verified_unchanged) -contains 'u59-preset.json') 'the manifest lists the preserved preset'
Assert-That ((Get-KytySha256 (Join-Path $taskCase.Backup 'kyty_emulator.exe')) -eq $taskBefore['kyty_emulator.exe']) 'the replaced binary is in the backup'
Assert-That ((Get-KytySha256 (Join-Path $taskCase.Backup '_SaveData\PPSA21564\SAVEDATA00\sdimg_save.bin')) -eq $taskBefore['_SaveData\PPSA21564\SAVEDATA00\sdimg_save.bin']) 'saves are copied into the backup'

# 3. A copy failure part-way restores the previous runtime exactly.
foreach ($taskFailAfter in @(0, 3, 7)) {
    $taskCase = New-Fixture -Ini
    $taskBefore = Get-TreeHashes $taskCase.Destination
    $taskPlan = New-Plan $taskCase -GraphicsProfile Performance1080p
    Assert-Throws { Install-Fixture $taskCase $taskPlan @{ FailAfter = $taskFailAfter } } 'previous runtime was restored' "an injected failure after $taskFailAfter operations is rolled back"
    Assert-That (Test-SameTree $taskBefore (Get-TreeHashes $taskCase.Destination)) "the runtime is byte-identical after rollback ($taskFailAfter operations)"
    Assert-That (!(Test-Path -LiteralPath (Join-Path $taskCase.Destination $KytyInstallJournalName))) 'a rolled-back update leaves no journal'
}
# The final manifest write is the last operation; failing it also rolls everything back.
$taskCase = New-Fixture -Ini
$taskBefore = Get-TreeHashes $taskCase.Destination
$taskPlan = New-Plan $taskCase -GraphicsProfile Performance1080p -Clock Adaptive
$taskOperations = $taskPlan.Files.Count + $taskPlan.Writes.Count + $taskPlan.Moves.Count
Assert-Throws { Install-Fixture $taskCase $taskPlan @{ FailAfter = $taskOperations } } 'previous runtime was restored' 'a failed manifest write is rolled back'
Assert-That (Test-SameTree $taskBefore (Get-TreeHashes $taskCase.Destination)) 'the runtime is byte-identical after a failed manifest write'

# 4. An interrupted update blocks the next one until it is restored.
$taskCase = New-Fixture -Ini
$taskBefore = Get-TreeHashes $taskCase.Destination
$taskPlan = New-Plan $taskCase -Remove -GraphicsProfile Accurate1440p
Assert-Throws { Install-Fixture $taskCase $taskPlan @{ InterruptAfter = 4 } } 'Injected interruption' 'an interruption leaves the update unfinished'
Assert-That (Test-Path -LiteralPath (Join-Path $taskCase.Destination $KytyInstallJournalName)) 'an interrupted update leaves its journal'
Assert-Throws { New-Plan $taskCase } 'did not finish' 'a later update refuses to run over an interrupted one'
Assert-That (Restore-KytyIncompleteInstall -Destination $taskCase.Destination) 'the interrupted update is restored'
Assert-That (Test-SameTree $taskBefore (Get-TreeHashes $taskCase.Destination)) 'the restored runtime is byte-identical to the previous one'
Assert-That (!(Restore-KytyIncompleteInstall -Destination $taskCase.Destination)) 'restoring twice is a no-op'

# 5. Removing an unselected package moves it into the backup; a later failure brings it back.
$taskCase = New-Fixture -Ini
$taskBefore = Get-TreeHashes $taskCase.Destination
$taskPlan = New-Plan $taskCase -Remove
Assert-That ($taskPlan.Packages['Experiments\DLSS'] -eq 'removed_to_backup' -and $taskPlan.Moves.Count -eq 2) 'the unselected package is planned for the backup'
$null = Install-Fixture $taskCase $taskPlan
Assert-That (!(Test-Path -LiteralPath (Join-Path $taskCase.Destination 'Experiments\DLSS'))) 'the removed package folder is gone'
Assert-That ((Get-KytySha256 (Join-Path $taskCase.Backup 'Experiments\DLSS\dlss_backend_tests.exe')) -eq $taskBefore['Experiments\DLSS\dlss_backend_tests.exe']) 'the removed package is in the backup'
$taskInfo = Get-Content -LiteralPath (Join-Path $taskCase.Destination 'BUILD-INFO.json') -Raw | ConvertFrom-Json
Assert-That ($taskInfo.dlss.installed_state -eq 'removed_to_backup' -and @($taskInfo.packages.'Experiments\DLSS'.installed_files.PSObject.Properties).Count -eq 0) 'the manifest reports the removed package without files'
$taskCase = New-Fixture -Ini
$taskBefore = Get-TreeHashes $taskCase.Destination
$taskPlan = New-Plan $taskCase -Remove
$taskOperations = $taskPlan.Files.Count + $taskPlan.Writes.Count + $taskPlan.Moves.Count
Assert-Throws { Install-Fixture $taskCase $taskPlan @{ FailAfter = $taskOperations } } 'previous runtime was restored' 'a failure after removing a package is rolled back'
Assert-That (Test-SameTree $taskBefore (Get-TreeHashes $taskCase.Destination)) 'the removed package is restored by rollback'

# 6. Requested configuration changes touch only their own settings.
$taskCase = New-Fixture -Ini
$taskPresetBefore = Get-KytySha256 (Join-Path $taskCase.Destination 'u59-preset.json')
$taskPlan = New-Plan $taskCase -GraphicsProfile Performance1080p -Clock Adaptive
$null = Install-Fixture $taskCase $taskPlan
$taskIni = [IO.File]::ReadAllText((Join-Path $taskCase.Destination 'Kyty.ini'))
Assert-That ($taskIni -match '(?m)^screen_resolution=R1920X1080\r?$' -and $taskIni -match '(?m)^gpu_occlusion_accurate=false\r?$' -and $taskIni -match '(?m)^tessellation_enabled=true\r?$') 'the graphics profile sets its global options'
Assert-That ($taskIni -match '(?m)^owner_value=keep\r?$' -and $taskIni -match 'PPSA21564\\screen_resolution=R2560X1440' -and $taskIni -match 'game_dir=F:/PS5 GAMES') 'other INI settings and per-game overrides are kept'
Assert-That ((Get-Content -LiteralPath (Join-Path $taskCase.Backup 'Kyty.ini') -Raw) -match 'R1280X720') 'the previous INI is in the backup'
$taskPatch = Get-Content -LiteralPath (Join-Path $taskCase.Destination '_Patches\PPSA21564.json') -Raw | ConvertFrom-Json
$taskMods = @{}
foreach ($taskMod in $taskPatch.mods) { $taskMods[$taskMod.id] = $taskMod.enabled }
Assert-That ($taskMods['codex.astro.clock-adaptive'] -eq $true -and $taskMods['codex.astro.clock30'] -eq $false -and $taskMods['owner.mod'] -eq $false) 'the adaptive clock is selected and the owner mod kept'
Assert-That ((Get-KytySha256 (Join-Path $taskCase.Destination 'u59-preset.json')) -eq $taskPresetBefore) 'the preset stays byte-identical when other settings change'

# 7. A fresh runtime receives every staged file, including the preset.
$taskCase = New-Fixture -EmptyDestination
$taskPlan = New-Plan $taskCase
$null = Install-Fixture $taskCase $taskPlan
$taskAfter = Get-TreeHashes $taskCase.Destination
Assert-That ($taskAfter['u59-preset.json'] -eq (Get-KytySha256 (Join-Path $taskCase.Stage 'u59-preset.json'))) 'a fresh runtime receives the staged preset'
Assert-That ($null -ne $taskAfter['_Patches\PPSA21564.json'] -and $null -eq $taskAfter['Experiments\DLSS\dlss_backend_tests.exe']) 'a fresh runtime gets the patch but no unselected package'
$taskInfo = Get-Content -LiteralPath (Join-Path $taskCase.Destination 'BUILD-INFO.json') -Raw | ConvertFrom-Json
Assert-That ($taskInfo.dlss.installed_state -eq 'absent') 'an absent package is reported absent'

Write-Output ("Runtime installer checks: {0} passed in PowerShell {1}; fixtures {2}" -f $script:taskChecks, $PSVersionTable.PSVersion, $taskRoot)
if (!$KeepFixtures) {
    # Only this run's own disposable fixture folder.
    Remove-Item -LiteralPath $taskRoot -Recurse -Force
}
