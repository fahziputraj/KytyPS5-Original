function Get-KytyWindowsToolchain {
    param([string]$Repository, [string]$Configuration)
    if ($Configuration -and !(Test-Path -LiteralPath $Configuration)) { throw "Toolchain configuration is missing: $Configuration" }
    if (!$Configuration) { $Configuration = Join-Path $Repository '_Build\deps\windows-toolchain\toolchain.json' }
    if (Test-Path -LiteralPath $Configuration) {
        $taskTools = Get-Content -LiteralPath $Configuration -Raw | ConvertFrom-Json
    } else {
        # Compatibility with the original home-PC build environment.
        $taskLegacyRoot = 'C:\Users\fahzi\Documents\Codex\2026-10-15\he\work'
        $taskTools = [pscustomobject]@{
            CompilerBin = "$taskLegacyRoot\toolchain\LLVM\bin"
            Cmake = "$taskLegacyRoot\build-tools-python\cmake\data\bin\cmake.exe"
            Ninja = ''
            Glslang = "$taskLegacyRoot\toolchain\glslang\bin\glslangValidator.exe"
            MsvcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
            SdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
            SdkVersion = '10.0.19041.0'
            QtRoot = "$taskLegacyRoot\Qt\6.10.3\msvc2022_64"
        }
    }
    foreach ($taskRequired in @($taskTools.Cmake, $taskTools.Glslang,
        (Join-Path $taskTools.CompilerBin 'clang-cl.exe'), (Join-Path $taskTools.CompilerBin 'lld-link.exe'),
        (Join-Path $taskTools.CompilerBin 'llvm-lib.exe'), (Join-Path $taskTools.MsvcRoot 'include\vector'),
        (Join-Path $taskTools.MsvcRoot 'lib\x64\libcmt.lib'),
        (Join-Path $taskTools.MsvcRoot 'lib\x64\oldnames.lib'),
        (Join-Path $taskTools.MsvcRoot 'lib\x64\msvcrtd.lib'),
        (Join-Path $taskTools.SdkRoot "Include\$($taskTools.SdkVersion)\um\Windows.h"),
        (Join-Path $taskTools.QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
        if (!(Test-Path -LiteralPath $taskRequired)) {
            throw "Missing build dependency: $taskRequired. Configure paths in $Configuration."
        }
    }
    return $taskTools
}

function Invoke-KytyPgoSource {
    param([string]$Repository, [scriptblock]$Action)
    $taskSourceLink = 'C:\kyty-src19'
    $taskSourceRoot = [IO.Path]::GetFullPath($Repository)
    $taskPreviousTarget = $null
    $taskLinkChanged = $false
    if (Get-Process ninja,clang-cl,lld-link -ErrorAction SilentlyContinue) {
        throw 'Another native build is running; leave the PGO source junction alone.'
    }
    try {
        if (Test-Path -LiteralPath $taskSourceLink) {
            $taskLink = Get-Item -LiteralPath $taskSourceLink
            if ($taskLink.LinkType -ne 'Junction') { throw "$taskSourceLink must be a directory junction." }
            $taskPreviousTarget = [IO.Path]::GetFullPath([string]@($taskLink.Target)[0])
            if ($taskPreviousTarget -ne $taskSourceRoot) {
                [IO.Directory]::Delete($taskSourceLink)
                $taskLinkChanged = $true
                New-Item -ItemType Junction -Path $taskSourceLink -Target $taskSourceRoot | Out-Null
            }
        } else {
            $taskLinkChanged = $true
            New-Item -ItemType Junction -Path $taskSourceLink -Target $taskSourceRoot | Out-Null
        }
        & $Action $taskSourceLink
    } finally {
        if ($taskLinkChanged) {
            if (Test-Path -LiteralPath $taskSourceLink) {
                $taskCurrentLink = Get-Item -LiteralPath $taskSourceLink
                if ($taskCurrentLink.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath([string]@($taskCurrentLink.Target)[0]) -ne $taskSourceRoot) {
                    throw 'The PGO source junction changed; refusing to remove it.'
                }
                [IO.Directory]::Delete($taskSourceLink)
            }
            if ($taskPreviousTarget) { New-Item -ItemType Junction -Path $taskSourceLink -Target $taskPreviousTarget | Out-Null }
        }
    }
}
