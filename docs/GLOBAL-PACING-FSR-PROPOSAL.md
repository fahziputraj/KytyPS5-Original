# Global presentation controls and the FSR3 test prototype

Target for an eventual proposal: **Jetsku/KytyPS5**. Development starts on
`codex/test-global-pacing-fsr`, based on the working local U59 int19.1 build.
This document describes a test branch, not a claim of compatibility with every
game. Keep normal play's known settings, caches and saves when testing it.

## Changes usable across games

The launcher exposes **Frame delivery**, **Frame limit**, and **CPU scheduling**
in global and per-game graphics settings. Preset inherits existing launch
profiles; explicit options override them. The emulator's equivalent switches are:

```text
--presentation-clock independent --presentation-fps 0
```

Independent presentation delivers completed guest frames while guest vblank
continues at the configured rate. A frame cap does not change that clock. The
legacy fixed mode remains available. CPU scheduling is applied to the child's
environment before it initializes threads. Hybrid scheduling keeps graphics
helpers on P-cores and background workers on E-cores; a homogeneous CPU retains
its existing placement. These changes do not create more CPU simulation capacity.

Games which advance simulation once per rendered frame still need an engine
adapter. Applying Astro addresses to unrelated executables is not a global fix.

## Reusable simulation-clock bindings

`simulation_clock` now accepts a typed layout and explicit rate bounds:

```json
{
  "nominal_fps": 120,
  "minimum_fps": 60,
  "fields": [
    { "offset": "00100000", "value": "seconds_f64" },
    { "offset": "00200000", "value": "rate_f32" }
  ]
}
```

The addresses above are schema examples, not a usable game patch. Supported
encodings are `rate_f32`, `rate_f64`, `seconds_f32`, `seconds_f64` and
`microseconds_u64`. Offsets are module-relative hexadecimal strings without a
prefix. Every full field must be covered by an original non-wildcard byte guard
in the selected patch's `memory` writes. Overlapping/misaligned fields, invalid
rates and unknown encodings are rejected before any write. The presentation
limit must match the nominal rate rounded up to an integer.

Title, version, module and original-byte checks remain in force. The old string
layout remains readable for older Astro patches. The controller's pause reset
retains its configured bounds, rather than silently returning another engine to
60 Hz. Only real main-output guest flips update the simulation bindings. Menus,
system overlays and future generated images cannot manufacture guest frames.

The supplied Astro 01.007 adapter remains bounded to 30-60 FPS. Other engines
require independently verified layouts, setter behavior and physics limits.
Tests for 60/30/45 and 120/60/90 Hz validate controller accounting, not another
game's physics. Performance below an adapter's minimum FPS can still slow play.

## FSR3: actual GPU prototype, separate from game presentation

`Fsr3Backend` loads AMD's official FidelityFX 1.1.4 Vulkan library and records its
prepare and frame-generation dispatches into a dedicated host command buffer.
It uses `NO_SWAPCHAIN_CONTEXT_NOTIFY`; the prototype does not replace the game's
swapchain. Its caller must own synchronization, generated-frame presentation,
latency management and UI composition. Generated images must never advance guest
vblank, guest flip completion or the simulation controller.

The initial contract supports native-size RGBA8 SDR, finite forward-Z depth,
motion vectors, explicit camera basis/FOV/jitter/scale and HUD-free scene color.
Missing guide data is rejected. It does not fabricate depth, motion or camera
data from a final screenshot. No FSR option is advertised as working in games.

Prepare the pinned SDK (about 449 MiB downloaded; no installer or system changes):

```powershell
python tools/prepare-fidelityfx.py
powershell -NoProfile -File tools/build-windows-local.ps1 -Configure -Fsr3Probe
```

The preparation tool checks the pinned release archive SHA-256 before extracting
only headers, AMD's signed Vulkan library and its license under ignored `_Build`.
It neither vendors nor replaces a system Vulkan library. Check the extracted
DLL's AMD Authenticode signature before executing the optional test.

```powershell
_Build/windows-int19/fsr3_vulkan_probe.exe `
  _Build/deps/fidelityfx-1.1.4/sdk-package/PrebuiltSignedDLL/amd_fidelityfx_vk.dll `
  _Build/fsr3-smoke-generated.ppm
```

The offscreen test submits six frames with known synthetic geometry, depth,
motion and camera data, reads the generated images back, and checks that output
is populated and differs from the rendered frame after history warmup. It also
checks rejection of missing guides. This is an SDK/GPU integration test; it does
not establish in-game FG, doubled game speed, image quality, or a performance gain.

### Remaining game integration

1. Capture validated per-frame scene color, depth, motion and camera metadata
   from a title's rendering passes, retaining their GPU lifetimes through FG.
2. Preserve the scene/UI boundary, support the title's depth convention and
   actual render/output extents, and reset history on cuts, loading and resize.
3. Integrate generation and pacing with U59's multi-port flip groups and system
   overlays. Keep real and generated presentation statistics separate.
4. Validate graphics, input latency, pause/resize/device-loss cleanup and GPU
   cost in gameplay before enabling an opt-in game mode.

The available local title is Astro Bot PPSA21564 01.007. Its FSR guide resources
have not yet been verified. The researched forks do not supply a validated FSR
adapter for that exact executable. This is the specific remaining prerequisite,
not something that a global FPS setting can infer safely.

## Primary references and reviewed implementations

- [AMD FSR3 integration and Vulkan API](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/).
- [AMD frame interpolation inputs](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation/).
- [Pinned official SDK v1.1.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v1.1.4),
  source `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`.
- [Demon's Souls input capture and FSR experiment](https://github.com/todojuegos67-bot/KytyPS5/blob/59951805307f0dad6eb8f98d7fe72de28a25c1f3/src/local/frame-gen.h):
  shader hashes and camera layout are specific to that game, not global signatures.
- [Astro-related DLSS/FSR fork](https://github.com/iska0702/DLSSKytyPS5): its published runtime
  findings distinguish Astro's Playroom from unverified Astro Bot support.

## Proposal shape

Propose the global presentation controls and guarded clock-profile schema as
reviewable changes first. Keep the SDK backend/GPU prototype as a separate
experimental commit/follow-up. The test branch retains local build tools and
Astro-specific patches; a final PR should select the portable changes rather
than import local machine paths, presets or unverified game claims.
