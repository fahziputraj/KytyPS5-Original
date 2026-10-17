# Windows pacing preview 1 - experimental

This community test build is based on [Jetsku U59 int19.1](https://github.com/Jetsku/KytyPS5/releases/tag/u59-windows-20261013-int19.1).
It adds reusable presentation/CPU controls and a version-specific ASTRO BOT
clock experiment. **It is a prerelease, not an official or universally stable
KytyPS5 build.** Full-speed gameplay in every scene is not established.

## Download and start

1. Download **KytyPS5-Pacing-Preview-Windows-x64.zip** from this release's Assets.
   Extract the ZIP into a new, writable folder. Keep it separate from a working
   emulator installation; do not overwrite your saves or settings.
2. Use your own game files. No games, keys, firmware, saves or shader caches are
   included. Requirements: Windows x64, a Vulkan-capable GPU/driver and enough
   resources for the selected game. Tested locally on Windows 11 Pro with an
   i5-13500, RTX 5070 Ti 16 GB and 32 GB DDR5; other hardware is unverified.
3. For ASTRO BOT **PPSA21564 version 01.007.000**, open
   **Start-AstroBot-30FPS.cmd** for the fixed-30 baseline, or
   **Start-AstroBot-Adaptive.cmd** for the experimental 30-60 FPS clock.
   Enter the folder containing `eboot.bin` when prompted. You can also drag that
   folder onto either CMD file. The wrapper refuses an unrecognized executable.
4. To compare without a clock patch, use **Start-AstroBot-Original.cmd**.
   Other game versions can use this mode, but their simulation speed is not
   corrected. To try other titles, use **launcher.exe**, add your game folder in
   Global settings, then select a game. The generic launcher does not
   automatically enable the Astro clock patch.

The CMD files call the included PowerShell script with a process-only execution
policy setting; they do not change machine-wide policy. Inspect the included
script before running downloaded code. Administrator privileges are not required.

The included `Kyty.ini` starts with an empty library and stores preview settings
locally. Emulation saves/caches are created under the extracted folder. Back up
any existing save before copying it into this test installation. The wrapper
does not alter the executable on disk. `--amd-cpu` is Kyty's compatibility path
for AMD console instructions and is intentionally used on the tested Intel CPU.

### Optional command-line controls

```powershell
.\Start-AstroBot-Preview.ps1 -GamePath 'D:\My Games\ASTRO BOT' -Mode Adaptive -LogOutput
.\Start-AstroBot-Preview.ps1 -GamePath 'D:\My Games\ASTRO BOT' -Mode Fixed30 -CpuMode off
```

CPU modes: `on` prefers P-cores for graphics work while keeping E-cores for
background workers; `off` disables placement; `background` limits the policy to
background workers. `-Gpu 0` selects a specific Vulkan adapter if automatic
selection picks the wrong one. `-DryRun` checks the files/hash and prints the
planned launch without starting the game. `-LogOutput` creates
`_kyty-preview.txt`; review logs for personal paths before posting them.

## What to expect

- The fixed-30 profile is the default baseline; adaptive mode is a separate,
  explicit experiment. Both keep guest vblank at **60 Hz**. A display's VRR
  feature is separate from simulation timing and is not required by this patch.
- Adaptive mode targets **30-60 FPS**. Below its 30 FPS floor, slowdown can still
  happen. Scene-dependent dips, stalls, rendering issues and crashes remain
  possible; movement, physics and cutscenes need wider validation.
- The performance profile uses a 1920 x 1080 output setting, tessellation on,
  accurate occlusion off, and costly image readback/raw synchronization off.
  That setting does not prove a game's internal rendering resolution.
- Global presentation and CPU options can be used with other games. Keeping
  their game speed correct still requires separate verified engine profiles;
  **this does not apply Astro's clock addresses to every title**.
- First launches may compile shaders. Learned shader/pipeline warmup can help
  subsequent runs; no precompiled game cache is distributed.

**FSR frame generation is not enabled in games.** `Experiments/FSR3` contains an
optional offscreen Vulkan prototype and AMD's SDK library. `Run-FSR3-Test.cmd`
generates a small synthetic test image; it does not insert frames into gameplay
or raise this build's in-game FPS. This directory can be left unused.

## Validation and feedback

The Windows build passed eight timing, profile, configuration and CPU-placement
tests. The offscreen FSR probe passed six interpolation dispatches and output
readback on the RTX 5070 Ti. Earlier runtime checks booted Astro with a save copy
and checked 100 consistent typed-clock samples. These checks are limited; they
do not establish full-game accuracy or compatibility on other machines.

Please report CPU/GPU/RAM, Windows and driver versions, title/version, launch
mode, scene/camera direction, FPS range and whether movement still slows down.
Compare the same scene with the same cache state and similar background load.
The FPS number alone does not establish correct game speed.

## Source and credits

- [Experiment overview, recording limits and credits](https://github.com/fahziputraj/KytyPS5-Original/blob/feature/global-pacing-fsr/docs/ASTRO-PACING-EXPERIMENT.md)
- [Technical proposal and remaining FSR work](https://github.com/fahziputraj/KytyPS5-Original/blob/feature/global-pacing-fsr/docs/GLOBAL-PACING-FSR-PROPOSAL.md)
- [Source branch](https://github.com/fahziputraj/KytyPS5-Original/tree/feature/global-pacing-fsr).
  The release tag and `BUILD-INFO.json` identify the exact source. For a source
  checkout use `git clone --recursive --branch pacing-preview-20261017.1 https://github.com/fahziputraj/KytyPS5-Original.git`.
  Build instructions are in the root README; local build options are documented
  in `docs/ASTRO-TIMING.md`. Dependency revisions are pinned in the source.

Credits: KytyPS5 contributors, Jetsku, original Kyty by InoriRus, related timing
research credited in the experiment notes, AMD GPUOpen for the optional SDK,
and fahziputraj/Kronoji for local testing. Research, code and documentation were
assisted by OpenAI Codex. ASTRO BOT belongs to TEAM ASOBI / Sony Interactive
Entertainment; no affiliation or endorsement is implied.

Project code is GPL-2.0; original Kyty notices and third-party licenses are
included. See `LICENSE`, `LICENSES` and `THIRD-PARTY-NOTICES.md` in the ZIP.
`SHA256SUMS.txt` in the release Assets verifies the downloads. This preview
contains unsigned community-built executables.
