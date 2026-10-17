# Local Windows build on E:

The current machine's runnable destination is `E:\KytyPS5-Build`. The existing
checkout remains at `E:\Project\KytyPS5-Original`; no extra checkout is needed.
Full compilation uses `_Build/windows-int19` and staging uses
`_Build/runtime-int19`, both on E:. The temporary `C:\kyty-src19` junction points
to this same checkout solely to preserve the bundled PGO profile's source names.
Build and installation each restore the junction's previous state in `finally`.
Installation also needs this alias because CMake records source-asset paths in
its install rules.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/update-build.ps1 `
  -Destination E:\KytyPS5-Build -Fsr3Probe -SkipFsr3GpuTest -Jobs 3
```

This builds the launcher, emulator, focused CPU tests and offscreen FSR probe.
The updater runs the CPU tests, verifies the pinned AMD DLL's hash/signature,
stages the runtime and copies it to the selected destination. On this development
machine `-SkipFsr3GpuTest` explicitly defers GPU execution; `BUILD-INFO.json`
records `gpu_probe_status: not_run`, and it does not claim gameplay validation.
FSR remains opt-in in the launcher. The separate
`Experiments/FSR3/Run-FSR3-Test.cmd` is available for supported-hardware testing.

Omit `-SkipFsr3GpuTest` to require that offscreen GPU test before delivery. Omit
both FSR switches for a build without bundling the optional test package.
For the home PC, pass `-Destination F:\KytyPS5-Build` explicitly. This machine's
build command does not write to that drive's runtime location.

## Toolchain configuration

`tools/windows-toolchain.ps1` reads the ignored local file
`_Build/deps/windows-toolchain/toolchain.json`, or a path supplied with
`-ToolchainConfig`. It contains these installed-tool paths:

| Property | Required path/value |
| --- | --- |
| `CompilerBin` | LLVM 22.1.3 `bin`, including clang-cl, lld-link and llvm-lib |
| `Cmake` | CMake executable |
| `Ninja` | Ninja executable; empty uses PATH |
| `Glslang` | glslang or glslangValidator executable |
| `MsvcRoot` | MSVC root containing `include` and `lib/x64` |
| `SdkRoot` | Windows Kits/10 root |
| `SdkVersion` | Installed SDK directory version |
| `QtRoot` | Qt 6.10.3 MSVC x64 package root |

LLVM, Qt, glslang and Microsoft C++ components on this machine are unpacked under
ignored `_Build/deps`; the already installed Windows SDK is reused. Compiler
selection does not depend on a system-wide Visual Studio installation. Without
a local configuration, the helper retains compatibility with the original home
PC's tool locations. Dependency source downloads use CMake's normal `_deps`
directories rather than paths on another computer.

## Delivery preservation

Existing destination presets, INI settings, caches and save data are retained.
A fresh destination starts from the repository's int19.1 preset. Overwritten
runtime files are backed up beside the destination in
`<destination>-Backups/<timestamp>-before-realtime-pacing`. Save files, if present,
are backed up and their hashes checked after installation. Explicit
`-GraphicsProfile` and `-LauncherAstroClock` options keep their existing meanings.
The fixed 30 FPS Astro wrapper and guarded 01.007 patches remain bundled.

Installation (`tools/runtime-install.ps1`) is transactional:

- **Preflight.** A requested graphics profile, the Astro patch merge, the staged
  file list and package selection are validated before the destination changes.
  A missing or malformed `Kyty.ini`, a patch for another game version, user data
  in the stage or a staged file of an unselected package stops the update with
  the runtime untouched.
- **Explicit file set.** The stage folder is reused between builds, so only the
  CMake install manifest plus the files the updater copies are installed. The
  list is written to `_Build/runtime-int19.files.txt`.
- **Backup, journal, rollback.** Each replaced file is copied to the backup and
  hash-checked, `KYTY-UPDATE-INCOMPLETE.json` records the update, and every
  installed file is verified against the stage. Any failure restores the previous
  files and removes created ones. A killed update leaves the journal, which blocks
  later updates until `tools/update-build.ps1 -Destination <dir>
  -RestoreIncompleteUpdate` rolls it back.
- **Optional packages.** A selected `Experiments\FSR3` or `Experiments\DLSS` package
  is updated; user files in it are kept and listed. An installed package that the
  build does not select is retained and reported as `retained` (with the build it
  came from), or moved to the backup with `-RemoveUnselectedPackages`.
  `BUILD-INFO.json` lists each package's actual files and hashes.
- **Byte preservation.** An installed `u59-preset.json` is never rewritten. The
  Astro patch is written only when its merged content changes. Saves are hashed;
  other `_*` user data (caches, trophies, patches) is fingerprinted by size and
  time. `Kyty.ini` and the preset are hash-checked unless a change was requested.

`tools/test-runtime-install.ps1` exercises these rules on disposable fixtures under
`_Build/test-runtime-install` (failure injection, interruption and restore, nested
or unknown preset fields, package retention and removal), and
`tools/test-astro-launcher.ps1` checks the Astro shortcut's title, version and
executable guards with dry runs. The updater runs both before staging; run them
directly with Windows PowerShell 5.1 and PowerShell 7 after editing the scripts.

A successful compile or launcher startup does not establish FSR/DLSS image
quality, generated-frame pacing, performance or game compatibility. See the
[FSR checkpoint](FSR-INTEGRATION-CHECKPOINT.md) and
[development guide](FSR-DLSS-DEVELOPMENT.md) for the remaining runtime checks.

## Verified on 2026-10-17

The full Windows x64 Release build completed with LLVM 22.1.3, release IPO,
the bundled int19 PGO profile, MSVC 14.51.36231 headers/libraries, Windows SDK
10.0.26100.0 and Qt 6.10.3. All 11 focused tests passed. The source alias was
checked against the profile's exact path spelling, and success, error-cleanup
and previous-junction restoration checks passed.

The installed emulator returned successfully from `--help`. The installed
launcher remained running for an eight-second startup check using a separate
local INI under `_Build/runtime-smoke`; normal settings were not used or changed.
The manifest hashes matched the installed emulator and launcher. The optional
FSR probe, signed AMD library and license were bundled, with GPU execution
explicitly recorded as not run. No game session was started.

The final build gate exposed intermittent CPU-placement test failures while a
newly assigned recorder thread was still moving to its selected CPU set. The
Windows test now verifies the selected IDs and allows at most one second for
that scheduling transition before sampling residency. All existing residency,
histogram and off-core assertions remain; emulator scheduling is unchanged.
Both recorder variants then passed five consecutive runs each, and the complete
11-test delivery suite passed with the focused change.

The owner's startup log on this machine reported Intel UHD Graphics 730 missing
`fragmentShaderBarycentric` and `shaderBufferInt64Atomics`. Kyty rejects that
device before guest rendering, independently of FSR being enabled. This machine
can compile the runtime and run CPU development tests, but the current renderer
cannot run the game here. The game folder on F: is Astro Bot 01.019 (PPSA21567),
which is outside the bundled PPSA21564 01.007 clock/temporal adapter checks. Do
not bypass the Vulkan feature requirements or apply those older game patches to
try to make this machine boot it. GPU execution and gameplay remain pending on
supported hardware.
