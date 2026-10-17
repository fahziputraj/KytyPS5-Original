# Transactional installation of a staged Kyty runtime into a user's runtime folder.
#
# New-KytyInstallPlan performs every read and validation without touching the destination.
# Invoke-KytyInstallPlan backs up each file it will replace, records a journal, installs and
# verifies hashes; any failure restores the previous files. A journal left by an interrupted
# update blocks later updates until Restore-KytyIncompleteInstall rolls it back.
#
# Package semantics: an optional package directory (Experiments\FSR3, Experiments\DLSS) that is
# selected is updated from the stage. An unselected package that is already installed is retained
# unchanged and reported as retained (with the previous manifest's description), or moved into
# the backup with -RemoveUnselectedPackages. Files in a package directory that the stage does not
# provide are never removed; the manifest lists them. User data (_*, Kyty.ini, an existing preset)
# is never part of the copy set; saves, settings and caches are verified unchanged afterwards.
# Requires Windows PowerShell 5.1 or PowerShell 7.

$KytyInstallJournalName = 'KYTY-UPDATE-INCOMPLETE.json'
$KytyPreservedIfPresent = @('u59-preset.json')

function Get-KytySha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-KytyRelativePath {
    param([string]$Root, [string]$Path)
    $taskRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $taskFull = [IO.Path]::GetFullPath($Path)
    if (!$taskFull.StartsWith($taskRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "$taskFull is outside $taskRoot" }
    return $taskFull.Substring($taskRoot.Length)
}

function Test-KytyUserDataPath {
    param([string]$Relative)
    return $Relative -match '^(_|Kyty\.ini$|KYTY-UPDATE-INCOMPLETE\.json$)'
}

function Get-KytyPackageOf {
    param([string]$Relative, [string[]]$Packages)
    foreach ($taskPackage in $Packages) {
        if ($Relative.StartsWith($taskPackage.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { return $taskPackage }
    }
    return $null
}

# Applies a graphics profile to the INI text; throws when the INI cannot be updated.
function Get-KytyGraphicsProfileIni {
    param([string]$Ini, [string]$GraphicsProfile)
    $taskMatch = [regex]::Match($Ini, '(?ms)^\[GlobalConfiguration\]\r?\n.*?(?=^\[|\z)')
    if (!$taskMatch.Success) { throw 'GlobalConfiguration was not found in Kyty.ini.' }
    $taskSection = $taskMatch.Value
    $taskResolution = $(if ($GraphicsProfile -eq 'Accurate1440p') { 'R2560X1440' } else { 'R1920X1080' })
    $taskAccuracy = $(if ($GraphicsProfile -eq 'Performance1080p') { 'false' } else { 'true' })
    $taskOptions = [ordered]@{ screen_resolution = $taskResolution; gpu_occlusion_accurate = $taskAccuracy;
        readback_linear_images = $taskAccuracy; sync_raw_image_buffers = $taskAccuracy; tessellation_enabled = 'true' }
    foreach ($taskOption in $taskOptions.GetEnumerator()) {
        $taskPattern = '(?m)^' + [regex]::Escape($taskOption.Key) + '=[^\r\n]*'
        $taskLine = $taskOption.Key + '=' + $taskOption.Value
        if ([regex]::IsMatch($taskSection, $taskPattern)) {
            $taskSection = [regex]::Replace($taskSection, $taskPattern, $taskLine)
        } else {
            $taskSection = $taskSection.TrimEnd() + "`r`n$taskLine`r`n`r`n"
        }
    }
    return $Ini.Substring(0, $taskMatch.Index) + $taskSection + $Ini.Substring($taskMatch.Index + $taskMatch.Length)
}

# The installed Astro patch merged with the staged clock profiles, or $null when nothing changes.
function Get-KytyAstroPatchUpdate {
    param([string]$Stage, [string]$Destination, [string]$BundledPatch, [string]$AdaptivePatch, [string]$LauncherAstroClock)
    $taskPatchDestination = Join-Path $Destination '_Patches\PPSA21564.json'
    $taskPatchDocument = Get-Content -LiteralPath (Join-Path $Stage $BundledPatch) -Raw | ConvertFrom-Json
    $taskExistingText = $null
    if (Test-Path -LiteralPath $taskPatchDestination) {
        $taskExistingText = Get-Content -LiteralPath $taskPatchDestination -Raw
        $taskExisting = $taskExistingText | ConvertFrom-Json
        if ($taskExisting.id -ne $taskPatchDocument.id -or $taskExisting.version -ne $taskPatchDocument.version -or $taskExisting.process -ne $taskPatchDocument.process) {
            throw 'An Astro patch for a different version is installed; refusing to overwrite it.'
        }
        $taskClockMod = $taskPatchDocument.mods[0]
        $taskPreviousClock = @($taskExisting.mods | Where-Object { $_.id -eq $taskClockMod.id })
        if ($taskPreviousClock.Count -eq 1) { $taskClockMod.enabled = $taskPreviousClock[0].enabled }
        $taskExisting.mods = @($taskExisting.mods | Where-Object { $_.id -ne $taskClockMod.id }) + @($taskClockMod)
        # Assign in place: Add-Member -Force would move the property and change the bytes.
        if ($taskExisting.PSObject.Properties['source_sha256']) {
            $taskExisting.source_sha256 = $taskPatchDocument.source_sha256
        } else {
            $taskExisting | Add-Member -NotePropertyName source_sha256 -NotePropertyValue $taskPatchDocument.source_sha256
        }
        $taskPatchDocument = $taskExisting
    }
    if ($LauncherAstroClock -ne 'Preserve') {
        $taskSelectedClock = $(if ($LauncherAstroClock -eq 'Adaptive') { 'codex.astro.clock-adaptive' } else { 'codex.astro.clock30' })
        foreach ($taskName in @($BundledPatch, $AdaptivePatch)) {
            $taskClockDocument = Get-Content -LiteralPath (Join-Path $Stage $taskName) -Raw | ConvertFrom-Json
            if ($taskClockDocument.id -ne $taskPatchDocument.id -or $taskClockDocument.version -ne $taskPatchDocument.version -or
                $taskClockDocument.process -ne $taskPatchDocument.process -or $taskClockDocument.source_sha256 -ne $taskPatchDocument.source_sha256) {
                throw 'The bundled Astro clock profiles do not match the installed title/version.'
            }
            $taskClockMod = $taskClockDocument.mods[0]
            $taskClockMod.enabled = $taskClockMod.id -eq $taskSelectedClock
            $taskPatchDocument.mods = @($taskPatchDocument.mods | Where-Object { $_.id -ne $taskClockMod.id }) + @($taskClockMod)
        }
    }
    $taskText = $taskPatchDocument | ConvertTo-Json -Depth 20
    if ($null -ne $taskExistingText) {
        # Keep the installed bytes when the merged document is semantically unchanged.
        $taskCurrent = $taskExistingText | ConvertFrom-Json | ConvertTo-Json -Depth 20
        if ($taskCurrent -ceq $taskText) { return $null }
    }
    return $taskText
}

# Cheap identity of user-data files that are not hashed (caches can be large).
function Get-KytyUserDataFingerprint {
    param([string]$Destination)
    $taskPrint = @{}
    Get-ChildItem -LiteralPath $Destination -Force | Where-Object { $_.Name -like '_*' -and $_.PSIsContainer -and $_.Name -ne '_SaveData' } | ForEach-Object {
        Get-ChildItem -LiteralPath $_.FullName -File -Recurse -Force | ForEach-Object {
            $taskPrint[(Get-KytyRelativePath $Destination $_.FullName)] = '{0}|{1}' -f $_.Length, $_.LastWriteTimeUtc.Ticks
        }
    }
    return $taskPrint
}

function Get-KytyPreviousManifest {
    param([string]$Destination)
    $taskPath = Join-Path $Destination 'BUILD-INFO.json'
    if (!(Test-Path -LiteralPath $taskPath)) { return $null }
    try { return Get-Content -LiteralPath $taskPath -Raw | ConvertFrom-Json } catch { return $null }
}

function New-KytyInstallPlan {
    param(
        [Parameter(Mandatory)][string]$Stage,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string[]]$StageFiles,
        [string[]]$SelectedPackages = @(),
        [string[]]$OptionalPackages = @('Experiments\FSR3', 'Experiments\DLSS'),
        [switch]$RemoveUnselectedPackages,
        [ValidateSet('Preserve','Performance1080p','Accurate1080p','Accurate1440p')][string]$GraphicsProfile = 'Preserve',
        [ValidateSet('Preserve','Fixed30','Adaptive')][string]$LauncherAstroClock = 'Preserve',
        [string]$BundledPatch = 'patches\PPSA21564-01.007-clock30.json',
        [string]$AdaptivePatch = 'patches\PPSA21564-01.007-clock-adaptive.json'
    )
    $Stage = [IO.Path]::GetFullPath($Stage).TrimEnd('\')
    $Destination = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
    if (!(Test-Path -LiteralPath $Destination -PathType Container)) { New-Item -ItemType Directory -Path $Destination -Force | Out-Null }
    $taskJournal = Join-Path $Destination $KytyInstallJournalName
    if (Test-Path -LiteralPath $taskJournal) {
        throw "An earlier update of $Destination did not finish. Restore it first (tools/update-build.ps1 -RestoreIncompleteUpdate)."
    }
    foreach ($taskPackage in $SelectedPackages) {
        if ($OptionalPackages -notcontains $taskPackage) { throw "Unknown optional package: $taskPackage" }
    }
    $taskFiles = New-Object System.Collections.Generic.List[object]
    $taskSeen = @{}
    foreach ($taskRelative in $StageFiles) {
        $taskRelative = $taskRelative.Replace('/', '\').TrimStart('\')
        if ($taskSeen.ContainsKey($taskRelative.ToLowerInvariant())) { continue }
        $taskSeen[$taskRelative.ToLowerInvariant()] = $true
        if (Test-KytyUserDataPath $taskRelative) { throw "Unexpected user data in runtime stage: $taskRelative" }
        $taskPackage = Get-KytyPackageOf $taskRelative $OptionalPackages
        if ($taskPackage -and $SelectedPackages -notcontains $taskPackage) { throw "Staged file belongs to an unselected package: $taskRelative" }
        $taskSource = Join-Path $Stage $taskRelative
        if (!(Test-Path -LiteralPath $taskSource -PathType Leaf)) { throw "Staged file is missing: $taskRelative" }
        $taskInstalled = Join-Path $Destination $taskRelative
        $taskExists = Test-Path -LiteralPath $taskInstalled -PathType Leaf
        if ((Test-Path -LiteralPath $taskInstalled) -and !$taskExists) { throw "A directory blocks the installed file: $taskRelative" }
        if ($taskExists -and $KytyPreservedIfPresent -contains $taskRelative) { continue }
        $taskFiles.Add([pscustomobject]@{ Relative = $taskRelative; Source = $taskSource; Existed = $taskExists; Hash = (Get-KytySha256 $taskSource) })
    }
    foreach ($taskRequired in @($BundledPatch, $AdaptivePatch)) {
        if (!$taskSeen.ContainsKey($taskRequired.ToLowerInvariant())) { throw "Staging is incomplete: $taskRequired" }
    }
    # Every configuration rewrite is computed before any destination change.
    $taskWrites = New-Object System.Collections.Generic.List[object]
    $taskIniPath = Join-Path $Destination 'Kyty.ini'
    if ($GraphicsProfile -ne 'Preserve') {
        if (!(Test-Path -LiteralPath $taskIniPath -PathType Leaf)) { throw "GraphicsProfile $GraphicsProfile requires an existing Kyty.ini in $Destination." }
        $taskIni = [IO.File]::ReadAllText($taskIniPath)
        $taskNewIni = Get-KytyGraphicsProfileIni -Ini $taskIni -GraphicsProfile $GraphicsProfile
        if ($taskNewIni -cne $taskIni) { $taskWrites.Add([pscustomobject]@{ Relative = 'Kyty.ini'; Text = $taskNewIni; Existed = $true }) }
    }
    $taskPatchRelative = '_Patches\PPSA21564.json'
    $taskPatchText = Get-KytyAstroPatchUpdate -Stage $Stage -Destination $Destination -BundledPatch $BundledPatch -AdaptivePatch $AdaptivePatch -LauncherAstroClock $LauncherAstroClock
    if ($null -ne $taskPatchText) {
        $taskWrites.Add([pscustomobject]@{ Relative = $taskPatchRelative; Text = $taskPatchText; Existed = (Test-Path -LiteralPath (Join-Path $Destination $taskPatchRelative)) })
    }
    $taskMoves = New-Object System.Collections.Generic.List[object]
    $taskPackages = [ordered]@{}
    $taskPrevious = Get-KytyPreviousManifest $Destination
    foreach ($taskPackage in $OptionalPackages) {
        $taskInstalledDir = Join-Path $Destination $taskPackage
        $taskPresent = Test-Path -LiteralPath $taskInstalledDir -PathType Container
        if ($SelectedPackages -contains $taskPackage) {
            $taskPackages[$taskPackage] = 'updated'
        } elseif (!$taskPresent) {
            $taskPackages[$taskPackage] = 'absent'
        } elseif ($RemoveUnselectedPackages) {
            $taskPackages[$taskPackage] = 'removed_to_backup'
            Get-ChildItem -LiteralPath $taskInstalledDir -File -Recurse -Force | ForEach-Object {
                $taskMoves.Add([pscustomobject]@{ Relative = (Get-KytyRelativePath $Destination $_.FullName) })
            }
        } else {
            $taskPackages[$taskPackage] = 'retained'
        }
    }
    $taskSaveRoot = Join-Path $Destination '_SaveData'
    $taskSaveHashes = @{}
    if (Test-Path -LiteralPath $taskSaveRoot) {
        Get-ChildItem -LiteralPath $taskSaveRoot -File -Recurse -Force | ForEach-Object {
            $taskSaveHashes[(Get-KytyRelativePath $Destination $_.FullName)] = Get-KytySha256 $_.FullName
        }
    }
    # Settings that this update must leave byte-identical.
    $taskPreserved = @{}
    $taskWritten = @($taskWrites | ForEach-Object { $_.Relative })
    foreach ($taskRelative in @('Kyty.ini') + $KytyPreservedIfPresent + @($taskPatchRelative)) {
        $taskPath = Join-Path $Destination $taskRelative
        if ($taskWritten -notcontains $taskRelative -and (Test-Path -LiteralPath $taskPath -PathType Leaf)) {
            $taskPreserved[$taskRelative] = Get-KytySha256 $taskPath
        }
    }
    return [pscustomobject]@{
        Stage = $Stage; Destination = $Destination; Files = $taskFiles.ToArray(); Writes = $taskWrites.ToArray(); Moves = $taskMoves.ToArray()
        Packages = $taskPackages; OptionalPackages = $OptionalPackages; PreviousManifest = $taskPrevious
        SaveHashes = $taskSaveHashes; PreservedHashes = $taskPreserved; UserDataFingerprint = (Get-KytyUserDataFingerprint $Destination)
    }
}

function Write-KytyUtf8File {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

# Rolls the destination back to the journaled backup. Files the update created are removed;
# replaced and moved files are restored from the backup.
function Invoke-KytyInstallRollback {
    param([string]$Destination, $Journal)
    $taskFailures = @()
    $taskEntries = @($Journal.entries)
    [array]::Reverse($taskEntries)
    foreach ($taskEntry in $taskEntries) {
        $taskInstalled = Join-Path $Destination $taskEntry.relative
        $taskSaved = Join-Path $Journal.backup $taskEntry.relative
        try {
            if ($taskEntry.existed) {
                if (!(Test-Path -LiteralPath $taskSaved -PathType Leaf)) { throw "backup copy is missing: $taskSaved" }
                New-Item -ItemType Directory -Path (Split-Path -Parent $taskInstalled) -Force | Out-Null
                Copy-Item -LiteralPath $taskSaved -Destination $taskInstalled -Force
                if ((Get-KytySha256 $taskInstalled) -ne $taskEntry.previous_sha256) { throw "restored hash differs: $($taskEntry.relative)" }
            } elseif (Test-Path -LiteralPath $taskInstalled -PathType Leaf) {
                Remove-Item -LiteralPath $taskInstalled -Force
            }
        } catch {
            $taskFailures += "$($taskEntry.relative): $($_.Exception.Message)"
        }
    }
    if ($taskFailures.Count) { throw "Rollback incomplete; restore manually from $($Journal.backup): $($taskFailures -join '; ')" }
}

function Restore-KytyIncompleteInstall {
    param([Parameter(Mandatory)][string]$Destination)
    $Destination = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
    $taskJournalPath = Join-Path $Destination $KytyInstallJournalName
    if (!(Test-Path -LiteralPath $taskJournalPath)) { return $false }
    $taskJournal = Get-Content -LiteralPath $taskJournalPath -Raw | ConvertFrom-Json
    Invoke-KytyInstallRollback -Destination $Destination -Journal $taskJournal
    Remove-Item -LiteralPath $taskJournalPath -Force
    return $true
}

function Get-KytyPackageReport {
    param($Plan, [string]$Package, [string]$State)
    $taskDir = Join-Path $Plan.Destination $Package
    $taskStaged = @{}
    foreach ($taskFile in $Plan.Files) { if ($taskFile.Relative.StartsWith($Package + '\', [StringComparison]::OrdinalIgnoreCase)) { $taskStaged[$taskFile.Relative.ToLowerInvariant()] = $true } }
    $taskInstalled = [ordered]@{}
    $taskOther = [ordered]@{}
    if (Test-Path -LiteralPath $taskDir -PathType Container) {
        Get-ChildItem -LiteralPath $taskDir -File -Recurse -Force | Sort-Object FullName | ForEach-Object {
            $taskRelative = Get-KytyRelativePath $Plan.Destination $_.FullName
            if ($State -eq 'updated' -and !$taskStaged.ContainsKey($taskRelative.ToLowerInvariant())) {
                $taskOther[$taskRelative] = Get-KytySha256 $_.FullName
            } else {
                $taskInstalled[$taskRelative] = Get-KytySha256 $_.FullName
            }
        }
    }
    $taskReport = [ordered]@{ installed_state = $State; installed_files = $taskInstalled }
    if ($taskOther.Count) { $taskReport['other_files_retained'] = $taskOther }
    return $taskReport
}

# Installs the plan. $Manifest (an ordered dictionary) receives the install report and is
# written as BUILD-INFO.json. FaultInjection is for tests: @{ FailAfter = n } throws after n
# operations (rolled back); @{ InterruptAfter = n } stops without rollback, like a killed process.
function Invoke-KytyInstallPlan {
    param(
        [Parameter(Mandatory)]$Plan,
        [Parameter(Mandatory)][string]$Backup,
        [System.Collections.IDictionary]$Manifest = $null,
        # Manifest sections (e.g. 'fsr3') that also receive their package's installed_state.
        [hashtable]$PackageManifestKeys = @{},
        [hashtable]$FaultInjection = @{}
    )
    $taskDestination = $Plan.Destination
    $taskJournalPath = Join-Path $taskDestination $KytyInstallJournalName
    if (Test-Path -LiteralPath $taskJournalPath) { throw "An earlier update of $taskDestination did not finish; restore it first." }
    if (Test-Path -LiteralPath $Backup) { throw "Backup folder already exists: $Backup" }
    New-Item -ItemType Directory -Path $Backup | Out-Null
    $taskEntries = New-Object System.Collections.Generic.List[object]
    $taskTargets = @($Plan.Files | ForEach-Object { [pscustomobject]@{ Relative = $_.Relative; Existed = $_.Existed } }) +
        @($Plan.Writes | ForEach-Object { [pscustomobject]@{ Relative = $_.Relative; Existed = $_.Existed } }) +
        @($Plan.Moves | ForEach-Object { [pscustomobject]@{ Relative = $_.Relative; Existed = $true } }) +
        @([pscustomobject]@{ Relative = 'BUILD-INFO.json'; Existed = (Test-Path -LiteralPath (Join-Path $taskDestination 'BUILD-INFO.json') -PathType Leaf) })
    $taskBackedUp = @{}
    foreach ($taskTarget in $taskTargets) {
        $taskKey = $taskTarget.Relative.ToLowerInvariant()
        if ($taskBackedUp.ContainsKey($taskKey)) { continue }
        $taskBackedUp[$taskKey] = $true
        $taskEntry = [ordered]@{ relative = $taskTarget.Relative; existed = [bool]$taskTarget.Existed; previous_sha256 = $null }
        if ($taskTarget.Existed) {
            $taskInstalled = Join-Path $taskDestination $taskTarget.Relative
            $taskSaved = Join-Path $Backup $taskTarget.Relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $taskSaved) -Force | Out-Null
            Copy-Item -LiteralPath $taskInstalled -Destination $taskSaved -Force
            $taskEntry.previous_sha256 = Get-KytySha256 $taskInstalled
            if ((Get-KytySha256 $taskSaved) -ne $taskEntry.previous_sha256) { throw "Backup copy differs: $($taskTarget.Relative)" }
        }
        $taskEntries.Add($taskEntry)
    }
    # Unchanged settings and saves are also kept beside the backup, as before.
    $taskIni = Join-Path $taskDestination 'Kyty.ini'
    if (!$taskBackedUp.ContainsKey('kyty.ini') -and (Test-Path -LiteralPath $taskIni -PathType Leaf)) { Copy-Item -LiteralPath $taskIni -Destination $Backup }
    $taskSaveRoot = Join-Path $taskDestination '_SaveData'
    if (Test-Path -LiteralPath $taskSaveRoot) { Copy-Item -LiteralPath $taskSaveRoot -Destination $Backup -Recurse }
    $taskJournal = [ordered]@{ backup = $Backup; started_at = [DateTimeOffset]::Now.ToString('o'); entries = $taskEntries.ToArray() }
    Write-KytyUtf8File -Path $taskJournalPath -Text ($taskJournal | ConvertTo-Json -Depth 6)
    $taskJournalObject = Get-Content -LiteralPath $taskJournalPath -Raw | ConvertFrom-Json
    $taskCounter = @{ Operations = 0 }
    $taskStep = {
        $taskCounter.Operations++
        if ($FaultInjection.ContainsKey('InterruptAfter') -and $taskCounter.Operations -gt $FaultInjection.InterruptAfter) {
            throw [System.OperationCanceledException]::new('Injected interruption')
        }
        if ($FaultInjection.ContainsKey('FailAfter') -and $taskCounter.Operations -gt $FaultInjection.FailAfter) {
            throw 'Injected copy failure'
        }
    }
    try {
        foreach ($taskFile in $Plan.Files) {
            & $taskStep
            $taskInstalled = Join-Path $taskDestination $taskFile.Relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $taskInstalled) -Force | Out-Null
            Copy-Item -LiteralPath $taskFile.Source -Destination $taskInstalled -Force
        }
        foreach ($taskWrite in $Plan.Writes) {
            & $taskStep
            Write-KytyUtf8File -Path (Join-Path $taskDestination $taskWrite.Relative) -Text $taskWrite.Text
        }
        foreach ($taskMove in $Plan.Moves) {
            & $taskStep
            # The backup already holds a verified copy; removing the installed file completes the move.
            Remove-Item -LiteralPath (Join-Path $taskDestination $taskMove.Relative) -Force
        }
        foreach ($taskPackage in $Plan.OptionalPackages) {
            $taskDir = Join-Path $taskDestination $taskPackage
            if ($Plan.Packages[$taskPackage] -eq 'removed_to_backup' -and (Test-Path -LiteralPath $taskDir) -and
                !(Get-ChildItem -LiteralPath $taskDir -Recurse -File -Force)) {
                # Only the now-empty package folders.
                Get-ChildItem -LiteralPath $taskDir -Recurse -Directory -Force | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object { [IO.Directory]::Delete($_.FullName) }
                [IO.Directory]::Delete($taskDir)
            }
        }
        # Verify the result before declaring success.
        foreach ($taskFile in $Plan.Files) {
            if ((Get-KytySha256 (Join-Path $taskDestination $taskFile.Relative)) -ne $taskFile.Hash) { throw "Installed file differs from the stage: $($taskFile.Relative)" }
        }
        foreach ($taskWrite in $Plan.Writes) {
            if ([IO.File]::ReadAllText((Join-Path $taskDestination $taskWrite.Relative)) -cne $taskWrite.Text) { throw "Written configuration differs: $($taskWrite.Relative)" }
        }
        foreach ($taskSaved in $Plan.PreservedHashes.GetEnumerator()) {
            if ((Get-KytySha256 (Join-Path $taskDestination $taskSaved.Key)) -ne $taskSaved.Value) { throw "Preserved setting changed: $($taskSaved.Key)" }
        }
        foreach ($taskSave in $Plan.SaveHashes.GetEnumerator()) {
            if ((Get-KytySha256 (Join-Path $taskDestination $taskSave.Key)) -ne $taskSave.Value) { throw "Save changed during deployment: $($taskSave.Key)" }
        }
        $taskPrint = Get-KytyUserDataFingerprint $taskDestination
        $taskWritten = @($Plan.Writes | ForEach-Object { $_.Relative })
        foreach ($taskEntry in $Plan.UserDataFingerprint.GetEnumerator()) {
            if ($taskWritten -notcontains $taskEntry.Key -and $taskPrint[$taskEntry.Key] -ne $taskEntry.Value) { throw "User data changed during deployment: $($taskEntry.Key)" }
        }
        if ($null -ne $Manifest) {
            $taskPackageReports = [ordered]@{}
            foreach ($taskPackage in $Plan.OptionalPackages) {
                $taskReport = Get-KytyPackageReport -Plan $Plan -Package $taskPackage -State $Plan.Packages[$taskPackage]
                if ($Plan.Packages[$taskPackage] -eq 'retained' -and $null -ne $Plan.PreviousManifest) {
                    $taskReport['retained_from_commit'] = $Plan.PreviousManifest.commit
                }
                $taskPackageReports[$taskPackage] = $taskReport
                if ($PackageManifestKeys.ContainsKey($taskPackage) -and $Manifest.Contains($PackageManifestKeys[$taskPackage])) {
                    $Manifest[$PackageManifestKeys[$taskPackage]]['installed_state'] = $taskReport.installed_state
                }
            }
            $Manifest['packages'] = $taskPackageReports
            $Manifest['backup'] = $Backup
            $Manifest['installed_files_verified'] = $Plan.Files.Count
            $Manifest['configuration_written'] = @($Plan.Writes | ForEach-Object { $_.Relative })
            $Manifest['settings_verified_unchanged'] = @($Plan.PreservedHashes.Keys | Sort-Object)
            $Manifest['save_files_verified_unchanged_at_install'] = $Plan.SaveHashes.Count
            $Manifest['user_data_files_verified_unchanged'] = $Plan.UserDataFingerprint.Count
            & $taskStep
            Write-KytyUtf8File -Path (Join-Path $taskDestination 'BUILD-INFO.json') -Text ($Manifest | ConvertTo-Json -Depth 8)
        }
    } catch [System.OperationCanceledException] {
        # Simulated process termination: no rollback, the journal stays.
        throw
    } catch {
        $taskError = $_
        try {
            Invoke-KytyInstallRollback -Destination $taskDestination -Journal $taskJournalObject
            Remove-Item -LiteralPath $taskJournalPath -Force
        } catch {
            throw "Update failed ($($taskError.Exception.Message)) and $($_.Exception.Message)"
        }
        throw "Update failed and the previous runtime was restored: $($taskError.Exception.Message)"
    }
    Remove-Item -LiteralPath $taskJournalPath -Force
}
