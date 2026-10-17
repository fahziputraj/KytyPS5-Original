# DLSS Vulkan SR backend: local development checkpoint

This is an opt-in, offscreen Super Resolution backend. It is not connected to
game presentation or exposed as a working launcher option. NVIDIA frame
generation, Multi Frame Generation and Ray Reconstruction are not implemented.
The normal Astro fixed-30 profile and existing FSR presentation path are unchanged.

## Implementation

`dlssBackend.h/.cpp` provides SDK-independent validation and frame-history rules.
`dlssNgx.h/.cpp` implements NVIDIA's NGX Vulkan discovery, initialization,
capability/driver queries, optimal-size validation, feature creation/evaluation
and cleanup. Required extensions are queried before instance/device creation and
checked against the actual enabled names. The physical device vendor is checked
through Vulkan before NGX initialization. One active adapter is allowed, with
serialized NGX calls and per-device shutdown after GPU completion.

The caller supplies its own GUID project identity, engine version, writable
absolute data directory and absolute feature-search directory. No other game's
application ID is used. NVIDIA SDK headers and library stay in ignored `_Build`.
They retain NVIDIA's license; external distribution needs a separate license
review, particularly because this repository contains GPL code. No release or
external redistribution is authorized by a successful local test.

The prototype deliberately accepts a narrow contract:

- HUD-free RGBA8 UNORM SDR color, forward-Z R32 float depth and RG16 float motion.
- Fixed, matching render/output aspect ratios; native-size DLAA or SR dimensions
  within the runtime's reported quality bounds.
- Unjittered low-resolution motion, explicit signed motion-to-pixel scale and
  camera jitter already expressed in render-pixel units. Verify their convention
  in a real input provider; this adapter does not infer it from final images.
- Separate single-sample, single-mip, single-layer 2D image views. Inputs need
  sampled usage and shader-read-only layout; output needs storage usage and
  general layout. Caller guarantees each view matches the supplied image.
- Automatic exposure, finite camera metadata and frame intervals up to 100 ms.

The caller owns resource barriers, queue ordering, lifetimes, command submission,
original-image fallback and UI composition. Use a dedicated host command buffer.
Submit successful recordings in order before advancing to another frame. If a
successful recording is discarded, reinitialize the backend. If an SDK recording
fails, discard that command buffer; the backend then refuses further recording
until reinitialized so it cannot reuse an unexecuted feature initialization.
Metadata rejection only invalidates temporal history. Duplicate real IDs never
advance history; gaps, rewind, wrap, camera cuts and suspension reset it.

## Reproducible development checks

The pinned SDK is NVIDIA DLSS `v310.9.16`, commit
`9981c6350dc58c0ce21f024fd65e31e851c31689`:

```powershell
python tools/prepare-dlss.py
./tools/test-temporal.ps1 `
  -FsrSdkRoot "$PWD/_Build/deps/fidelityfx-1.1.4/sdk-package" `
  -DlssSdkRoot "$PWD/_Build/deps/dlss-310.9.16"
```

Preparation verifies per-file SHA-256 hashes for five headers, the x64 NGX
library and license. It does not download `nvngx_dlss.dll`, initialize NGX or
install drivers. CMake does not download any SDK automatically.

Without SDK roots, the standalone build includes five CPU suites. With both
SDKs it includes seven: the original four temporal/timing suites, FSR backend
guards, DLSS backend contracts and NGX mapping/failure tests. The NGX mapping
tests compile the actual adapter against NVIDIA headers and provide stub NGX and
Vulkan functions; no GPU or NVIDIA runtime is involved.

The native MSVC-ABI build also links NVIDIA's actual x64 NGX library:

```powershell
./tools/build-windows-local.ps1 -Targets dlss_backend_tests -DlssPrototype
./_Build/windows-int19/dlss_ngx_link_check.exe
```

The link check creates an adapter object and rejects an invalid discovery
request before any NGX call. It does not initialize the SDK, enumerate GPUs or
evaluate DLSS. This distinction is intentional and printed by the executable.

For local delivery, preserving settings, caches and saves:

```powershell
./tools/update-build.ps1 -Destination E:\KytyPS5-Build `
  -Fsr3Probe -SkipFsr3GpuTest -DlssPrototype -Jobs 3
```

The optional `Experiments/DLSS` directory contains the two CPU test executables,
the real-library link check, `Run-DLSS-Checks.cmd`, this guide and NVIDIA's license.
The build manifest records GPU execution as `not_run`, no feature DLL bundled,
no in-game enablement and no DLSS frame-generation implementation.

## Offscreen SR probe (hardware gate)

`tests/DlssVulkanProbe.cpp` builds `dlss_vulkan_probe` with the NGX link targets
(`-DlssPrototype`) and is packaged in `Experiments/DLSS` with `Run-DLSS-Probe.cmd`.
It is not a CTest. Run it on an NVIDIA RTX PC with NVIDIA's official signed
`nvngx_dlss.dll` in a folder you choose:

```powershell
./_Build/windows-int19/dlss_vulkan_probe.exe <folder with nvngx_dlss.dll> <data dir> out.ppm [--validation]
./tools/update-build.ps1 -Destination <runtime> -Fsr3Probe -DlssPrototype `
  -DlssGpuProbe -DlssFeatureDirectory <folder with nvngx_dlss.dll> -Jobs 3
```

Before any NGX call it requires the feature DLL and an NVIDIA Vulkan GPU, and
otherwise exits 77 (SKIP). It then records the DLL's SHA-256, file version and
Authenticode signer (refusing an unsigned or non-NVIDIA file), queries NGX
instance requirements, creates the instance, queries device support/extensions,
creates the device with those extensions and every supported core feature, and
runs `DlssBackend` with the real NGX adapter:

- 640x360 to 1280x720 Performance: eight consecutive frames of a moving square
  with Halton jitter, forward-Z depth and current-to-previous motion in render
  pixels (`InMVScale` 1); the output is cleared to a sentinel first, and every
  frame must overwrite it and keep the static background. From the fourth frame
  the square must be at its upscaled position and size;
- a camera cut with reset (no ghost at the old position), duplicate-frame
  rejection without SDK work, a frame-ID gap, a successful recording that is
  discarded and followed by reinitialization, an invalid project ID and a second
  simultaneous adapter (both rejected);
- 960x540 to 1440x810 Quality as a second configuration;
- with `--validation`, no Khronos validation-layer errors.

It prints the GPU, driver, NGX extension counts and a `REPORT {...}` line, and
saves the last output as PPM. `-DlssGpuProbe` makes the updater run it and fail
on a skip; the manifest then records `gpu_execution: offscreen_probe_passed` and
the report. The project ID `624b3d1a-e585-43fa-a5d9-19fb002c698e` (engine version
`kyty-dlss-probe-1`) is a GUID generated for this repository's prototype, not an
NVIDIA-issued application ID. The jitter and motion conventions follow NVIDIA's
programming guide; the coarse position/size checks do not prove their signs, so
inspect the saved image and compare with a deliberately wrong sign if in doubt.

On the iGPU development PC the probe built and linked against the pinned NGX
library; its missing-runtime and no-NVIDIA-GPU paths exited 77 before any NGX
call, and `--runtime-info` verified signature/hash code against the signed AMD
FSR DLL (a stand-in signer, not an NVIDIA runtime). No DLSS GPU work has run.

## Remaining RTX work

Local verification on 2026-10-17: the seven standalone developer suites passed;
DLSS backend tests reported 102 checks and NGX mapping tests reported 49 checks.
The native Windows build compiled the adapter, linked the pinned NVIDIA library,
and passed all 14 focused delivery tests. No DLSS GPU initialization, evaluation,
image readback, gameplay, quality or performance result is established by these
checks. The emulator and launcher do not link or activate this NGX prototype.

Run the offscreen probe above on an RTX GPU with NVIDIA's signed feature runtime
(first with `--validation`), keep its output image, log and `REPORT` line, and
fix whatever it finds before any game integration. API/header linkage alone does
not establish a loaded-runtime version or GPU behavior.

Then provide verified game scene inputs before integrating into presentation.
The existing Astro FSR compatibility source retains HUD/TAA and must not be
passed into this HUD-free SR backend without a deliberate, validated provider.
PPSA21567 01.019 on the iGPU machine is not certified by the older 01.007 adapter.
Only after SR is validated should a separate DLSS FG integration be attempted.

References: [official pinned SDK](https://github.com/NVIDIA/DLSS/tree/9981c6350dc58c0ce21f024fd65e31e851c31689),
[NGX Vulkan API](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx_vk.h),
[integration guide](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/doc/DLSS_Programming_Guide_Release.pdf).
