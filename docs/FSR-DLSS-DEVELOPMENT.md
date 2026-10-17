# FSR and DLSS development without an RTX GPU

Use the existing checkout and `feature/global-pacing-fsr`. An iGPU development
machine can compile code and run CPU tests. A supported RTX machine is needed
to validate NVIDIA DLSS dispatch and the applicable frame-generation features.
The existing FSR GPU probe and in-game swapchain integration remain separate
runtime checks. CPU tests cannot establish image quality, GPU resource lifetime,
latency, frame-generation output or a performance gain.

## Developer tests

The standalone project in `tests/temporal` builds only these checks:

- Temporal input metadata, image descriptions and history transitions.
- Captured Astro camera decoding and reprojection mathematics.
- Presentation timing, guest vblank separation and bounded simulation clocks.
- Typed simulation-clock profile validation and original-byte guard coverage.

It uses the checked-in Vulkan and JSON headers. It does not need Qt, a Vulkan
runtime, shader compilation, the FidelityFX DLL, games or saves, and makes no
network requests. It does not load a GPU driver. Supply a C++20 compiler, CMake
3.21 or later and a build tool such as Ninja. For MSVC, use an x64 developer
PowerShell so the compiler and Windows SDK paths are initialized.

```powershell
./tools/test-temporal.ps1
```

For a compiler that is not on PATH:

```powershell
./tools/test-temporal.ps1 -CxxCompiler 'C:/LLVM/bin/clang-cl.exe'
```

A portable Zig installation can run these CPU checks without MSVC libraries:

```powershell
./tools/test-temporal.ps1 -CxxCompiler 'C:/tools/zig/zig.exe' -CxxCompilerArgument 'c++'
```

The paths above are examples; use installed paths. The helper never installs
tools or changes global environment settings. `-Cmake` selects a CMake executable
and `-Generator` selects a generator. If CMake is absent from PATH, the default
command can reuse its path from an existing developer-build cache. Keep the same compiler/generator for an
existing build directory; CMake will report a mismatch rather than deleting it.
Output is reused at `_Build/windows-dev`, with CTest results in
`_Build/windows-dev/temporal-tests.xml`. This is separate from the full emulator's
normal `_Build/windows-int19` PGO build.

To compile both FSR implementation files without executing the SDK, prepare the
pinned SDK using the existing preparation tool, then enable its headers:

```powershell
python tools/prepare-fidelityfx.py
./tools/test-temporal.ps1 -FsrSdkRoot "$PWD/_Build/deps/fidelityfx-1.1.4/sdk-package"
```

The optional target compiles both backend implementations and links an additional
CPU guard test. That test exercises their unsupported-device, absent-context and
cleanup paths using a stub Vulkan entry-point query; it never calls a GPU driver
or AMD's DLL. It does not link an emulator or run the GPU probe. Omitting
`-FsrSdkRoot` disables that extra compile/test step again. This developer path
does not update the runnable destination; full runnable
updates continue through `tools/update-build.ps1` and its preservation checks.

## Shared temporal inputs and FSR fallback

`temporalInputs.h` describes Vulkan images, camera metadata and real-frame
history without depending on AMD or NVIDIA SDK headers. The FSR names remain
aliases so existing callers keep their interface. The subsequent optional
[DLSS backend checkpoint](DLSS-BACKEND.md) adds NGX Vulkan SR discovery,
capability checks and SDK dispatch bindings, with CPU/stub tests and a real-library
link check. It is not connected to game presentation. DLSS GPU execution and
frame generation remain unverified/unimplemented respectively.

Before SDK configuration or dispatch, FSR validates finite camera values,
orthonormal camera axes, clipping planes, frame time, guide dimensions/formats
and requested SR output. Output images cannot alias the color, depth or motion
input. Invalid SR resources reject the frame instead of silently skipping SR
while enabling generation. The presentation compatibility path keeps its
HUD-retaining contract; the offscreen probe still requires HUD-free color.

Only successfully recorded real frames enter history. Duplicate IDs are rejected.
A camera reset, skipped/rewound ID, render-size change or suspension invalidates
continuity. Enabling generation after SR-only/overlay frames also requests a
reset. These rules do not advance guest flips, vblank or simulation clocks.

## Next development and home validation

Work possible here includes backend capability/fallback handling, DLSS SDK
integration and compile checks, diagnostics, launcher persistence and test
packaging. Keep the fixed 30 FPS Astro profile as the normal entry point. Do not
guess another game's guide resources or camera layout.

At home, build the full emulator with the required toolchain and run the pinned
offscreen probe first. Then test SR alone, FG alone and both on the exact supported
game version. Verify actual SDK presentation counts, camera motion, HUD/transparency,
movement pace, loading, pause, overlays, resize, fallback and shutdown. Measure
frame time and latency in addition to displayed FPS. Keep saves, settings and caches
intact, and distinguish a successful build from validated gameplay.

See [the integration checkpoint](FSR-INTEGRATION-CHECKPOINT.md) for earlier evidence
and the [global proposal](GLOBAL-PACING-FSR-PROPOSAL.md) for the SDK/input contracts.

## CPU foundation verification on 2026-10-17

On the iGPU development machine, portable Zig 0.16.0 (Clang 21.1.0), CMake and
Ninja compiled the four default suites. Both FSR backend implementations also
compiled against the pinned FidelityFX 1.1.4 headers, and the additional backend
guard test linked and passed. CTest reported **5/5 passing**, including **123
temporal-input checks**. PowerShell parsing and Git whitespace checks passed.
The prepared AMD DLL matched the repository's pinned SHA-256 and Windows
validated its AMD Authenticode signature; these tests did not load it.

The CPU-test tools live in the normal uv cache and SDK files in ignored
`_Build/deps`. At that CPU-only checkpoint no full emulator/launcher build, GPU
probe, DLSS execution or game session was run. No runtime was staged or installed.
The working Astro profile, game data, settings, shader caches and saves were not
modified. The original checkpoint's gameplay validation remains outstanding.

Subsequent full builds on this machine use `E:/KytyPS5-Build` and the separate
PGO toolchain described in [the Windows build guide](WINDOWS-LOCAL-BUILD.md).
Building the full runtime does not close the outstanding GPU/gameplay checks.
