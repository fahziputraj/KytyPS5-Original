# Experimental FSR integration checkpoint (2026-10-17)

This checkpoint connects FSR to the game presentation code. It is **not a
gameplay-validated release**. Keep the existing preview as the published build
until the checks below are complete.

Development without an RTX GPU can use the standalone CPU tests and optional
SDK compilation described in [FSR and DLSS development](FSR-DLSS-DEVELOPMENT.md).
That path does not provide the missing in-game runtime validation.

The optional FidelityFX 1.1.4 build now includes:

- Saved launcher and CLI selections for FSR upscaling and frame generation.
- A restricted Astro Bot PPSA21564 01.007 temporal-input adapter, using certified
  pixel-shader hashes, RGBA16F MRT4 motion, D32S8 depth, and a validated camera
  block. Render-target formats alone never enable the adapter.
- Frame-owned guide copies and conversion, separate from guest-memory access
  on the presentation thread.
- AMD's Vulkan replacement swapchain for generated presentation, with dedicated
  SDK queues. Generated presents do not advance guest flips or simulation clocks.
- Temporal-history reset and fallback paths for missing inputs, duplicate frames,
  long frame gaps, overlays, pause, and swapchain recreation.

The adapter retains the game's TAA, postprocessing and HUD. Its upscaling mode
resamples the completed game image before FSR reconstruction; it does **not**
reduce the game's original rendering work. HUD and transparent effects may have
interpolation artifacts. This is a compatibility experiment, not a replacement
for the game's own temporal renderer.

## Validation completed

The Windows Release build compiled and linked. Ten timing, CPU placement,
controller, launcher configuration and camera tests passed. The existing
offscreen probe completed six interpolation dispatches and generated-image
readback on an RTX 5070 Ti. The new replacement-swapchain path initialized in
the emulator. The subsequent game launch could not open the game executable
because the local installation was being replaced during extraction.

After that build, a small source adjustment preserved the original bitwise copy
for matching sRGB/UNORM formats when upscaling is inactive. That final adjustment
still needs a rebuild before runtime validation.

**No in-game FSR dispatch, generated presentation count, visual quality or
performance improvement has been verified for this checkpoint.** The existing
offscreen probe exercises a different dispatch path and does not establish those
results. The installed working build and published preview have not been replaced.

## Before publishing an updated preview

1. Obtain the extracted game's actual title/version and validate its adapter.
   Existing Astro timing patches remain restricted to their exact 01.007
   executable; newer versions require independent checks.
2. Exercise SR alone, FG alone, and both during gameplay. Verify actual SDK
   presentation counts, movement pace, image quality and resource lifetimes.
3. Test loading, pause, overlays, resize/fullscreen, missing SDK/unsupported GPU,
   dynamic resolution and shutdown. Verify that fallback retains the original
   image and that shader-state reuse remains correct after guide conversion.
4. Verify launcher saving/launch arguments in the UI, show real versus displayed
   frame rates, and review the shared input contract before enabling other games.
5. Update the release documentation and manifest from measured results; package
   a new immutable preview with licenses and checksums, excluding games, saves,
   caches and private diagnostics.

## Credits

Camera validation and motion-conversion analysis were adapted from
[iska0702/DLSSKytyPS5 at 8b763bd6](https://github.com/iska0702/DLSSKytyPS5/tree/8b763bd6fa14c08fe5b3417c3889b8522f23c5c3).
See the repository's GPL license and `LICENSES/Kyty-MIT.txt`.
The restricted 01.007 shader hashes were analyzed separately; the researched
fork's 01.018 adapter does not establish compatibility with other versions.
FidelityFX is provided by AMD under the SDK's bundled license.

The shared guides may support future DLSS SR/FG adapters. NVIDIA Multi Frame
Generation, ray tracing optimization and additional title adapters are separate,
unimplemented work; this checkpoint makes no claim of global game support.
