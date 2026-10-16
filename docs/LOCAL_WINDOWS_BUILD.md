# Local Windows build

The latest runnable build goes in **F:\KytyPS5-Build**. Each update replaces the
executables in that folder. Settings, shader caches and saves are preserved.

From the repository directory, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/update-build.ps1
```

This uses the existing local LLVM, Qt and Windows SDK installations, builds Release
with link-time optimization, runs the three focused resource/cache test suites,
and installs the launcher and its runtime dependencies in the destination folder.
`BUILD-INFO.json` records source revision, modified state and executable SHA-256.
Local builds are identified as Source builds. No changes are pushed upstream.

## Astro Bot PPSA21564 / 01.007.000

Run `Start-AstroBot.cmd` in the build folder, or open `kyty_launcher.exe`.
The direct shortcut uses the supplied H: game folder. It keeps red-zone protection,
uses the RTX 5070 Ti (Vulkan device 0), 60 Hz virtual vblank and FIFO presentation,
and disables AMD-specific patches, debug logs and optional image readbacks.
The 1280x720 dimensions control the emulator window; they do not change the game's
internal rendering resolution. No process affinity or system power settings are changed.

The original crash is also reported in
[upstream issue 1539](https://github.com/KytyPS5/KytyPS5/issues/1539).
Conditional scalar pointer reads in a floating-point-controlled BVH path were
being evaluated on the host. They now remain GPU reads, and captured speculative
reads use the checked guest-memory reader. Failed SRT refreshes name the shader.

Compute-shader preloading learns successful shader variants under `_ShaderCache`.
On subsequent launches it compiles their modules and pipelines before guest startup.
Live guest descriptors are refreshed during execution. Vertex/pixel shaders and
shaders containing dynamic guest calls still compile on demand. Learned recipes
are checksummed and bounded; compiler-source or GPU-driver changes invalidate them.
Set `KYTY_SHADER_PRELOAD=0` before launching to disable preloading.

The Vulkan pipeline cache is retained under `_PipelineCache` and checkpointed
every 30 seconds, plus after startup preloading and during clean shutdown.
Do not clear these caches between normal launches. Clearing `_ShaderCache` is
safe when deliberately rebuilding the learned recipe list.

## Verification and remaining limit

- Original official build: reproduced host read access violation at 0x18 after CS 15.
- Patched build: advanced beyond that lighting shader and rendered the PlayStation
  Studios startup sequence, observed around 56-60 FPS during that sequence.
- Shader validation accepted the translated lighting shaders.
- Resource-tracking, resource-materialization and shader-preload tests pass.
- Warm launch: 41 compute shaders and pipelines preloaded in 13,494 ms on this rig.
- A later run failed with `ErrorDeviceLost` while waiting on the Vulkan timeline,
  after VS 22 / PS 32 / CS 46 / GS 2. Enabling both image synchronization options
  did not resolve it. Full gameplay and sustained FPS are **not verified**.

The original installation at F:\kitty PS5-171026 is retained. This build fixes the
reported host crash and adds preloading; it does not claim complete Astro Bot compatibility.
