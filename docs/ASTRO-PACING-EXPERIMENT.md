# Astro Bot: variable FPS with steadier game speed

This is an experimental local branch based on [Jetsku's U59 int19.1 release](https://github.com/Jetsku/KytyPS5/releases/tag/u59-windows-20261013-int19.1).
The aim is to reduce the slow-motion effect when frame delivery falls below the
game's normal rate. It is not a universal compatibility patch or a finished
performance release. The underlying emulator and most renderer optimizations
come from KytyPS5 and Jetsku's fork.

## What the October 17 recording shows

The Sky Garden recording was captured around 06:58-07:01 WIB on October 17, 2026.
It shows the earlier Astro-specific adaptive experiment, before the later global
launcher controls and isolated FSR GPU prototype were added. The prior adaptive
controller is in commit `4b767245`; the fullscreen recording itself does not
display a source-build hash, so that is a development reference rather than an
independently identified recording build.

The player reported improved gameplay pace after the adaptive timing changes.
The footage also contains drops below 30 FPS. A frame counter and subjective
movement checks do not prove accurate physics or timing in every scene. This is
a progress demonstration, not a controlled benchmark or a locked-60-FPS result.

## Test machine and setup

| Item | Setup |
| --- | --- |
| CPU | Intel Core i5-13500 |
| GPU | NVIDIA GeForce RTX 5070 Ti, 16 GB VRAM |
| Memory | 32 GB DDR5 |
| OS / graphics API | Windows 11 Pro / Vulkan |
| Game | ASTRO BOT, PPSA21564, version 01.007.000 |
| Location | Sky Garden |
| Emulator output setting | 1920 x 1080 |
| Capture file | 2560 x 1440, 60 FPS, H.264/AAC, approximately 2m50s |
| Graphics profile | Performance; tessellation on; accurate occlusion, linear-image readback and raw-image synchronization off |
| CPU scheduling | CP on logical 2-3; graphics helpers on remaining P-cores; background compilation on E-cores |
| Timing target | Adaptive 30-60 FPS with guest vblank retained at 60 Hz |

The capture dimensions and capture frame rate are not the game's internal render
resolution or a claim that gameplay maintains 60 FPS. E-cores remain enabled.
Performance varies with scene, camera direction, cache state and background work.

## What changes

Independent presentation separates delivery of completed guest frames from the
guest's display-event clock. On its own, that did not remove Astro's slow motion.
The version-specific adapter also retains and adjusts the engine's timestep
values, using bounded smoothing as real frame delivery changes.

The current Astro profile targets 30-60 FPS. Below 30 FPS, its maximum timestep
can no longer compensate for every missed frame, so slowdown can still occur.
Large stalls and pauses reset the controller's timing history rather than
creating an unlimited catch-up burst. Tests validate this accounting; they do
not establish correct gameplay physics throughout the game.

The adapter checks the title, game version, executable module and original bytes.
The launch wrapper also verifies the executable hash. It patches loaded memory,
not the game file on disk. Its addresses must not be reused for unrelated games.
See the [timing investigation](ASTRO-TIMING.md) for implementation history and
the [current reusable profile contract](GLOBAL-PACING-FSR-PROPOSAL.md) for the
typed field format. Some older investigation sections describe rejected
accuracy settings; the performance setup above is the configuration described
for this demonstration.

## Current branch and validation

The later `feature/global-pacing-fsr` work adds global and per-game presentation,
FPS-limit and CPU-scheduling controls, plus reusable, guarded simulation-clock
bindings. The controls are reusable; another game's pace correction still needs
its own verified engine layout and gameplay testing. Only this Astro version is
locally runtime tested.

At code commit `98119a43`, the Windows Release build passed eight relevant tests:
presentation timing, presentation configuration, typed simulation profiles,
launcher settings persistence/inheritance and four CPU placement modes. Six CLI
argument checks also passed. The installed build booted Astro with a save copy;
100 runtime samples confirmed consistent typed clock fields. These are scoped
checks, not full compatibility certification. Linux and other titles have not
been validated for this experimental branch.

## FSR frame generation status

FSR frame generation is **not enabled in the game and is not demonstrated in the
recording**. An optional standalone backend uses AMD's official FidelityFX SDK
1.1.4. Its offscreen Vulkan probe passed six interpolation dispatches and output
readback on the RTX 5070 Ti using known synthetic color, depth, motion and camera
inputs. That does not establish in-game image quality, latency or a performance
gain. It also does not make guest simulation faster.

Remaining work includes verified per-frame Astro guide capture, correct HUD
handling, generated-frame scheduling, history resets and gameplay validation.
The [FSR proposal notes](GLOBAL-PACING-FSR-PROPOSAL.md) describe the optional test,
input contract, reviewed forks and primary SDK references.

## Next steps

- Compare movement and repeatable animations against elapsed time across more
  scenes, including pause/resume, cutscenes and heavy camera views.
- Improve transitions and frame pacing while preserving bounded engine steps.
- Validate separate clock profiles for additional titles.
- Integrate and evaluate FSR with actual game inputs before advertising it as a
  playable option.
- Prepare focused, portable proposals for Jetsku's fork. The experimental branch
  includes local build tools and is not itself an upstream-ready pull request.

## Credits

- [KytyPS5 contributors](https://github.com/KytyPS5/KytyPS5): emulator foundation.
- [Jetsku/KytyPS5](https://github.com/Jetsku/KytyPS5): U59 int19.1 base, renderer,
  caching and performance work used by this branch.
- [InoriRus/Kyty](https://github.com/InoriRus/Kyty): original Kyty project.
- Related timing research: [chenxiao07](https://github.com/chenxiao07/KytyPS5/commit/eadf554fbd49a2cc4119b374a926ad3c01128aa7),
  [RainKikyou](https://github.com/RainKikyou/KytyPS5/commit/aac0f9f5e7c5cd31ce5e0aeddd428147584017a6),
  and [bigmak94/AstroQuest](https://github.com/bigmak94/AstroQuest). These are
  research references, not claims that every implementation was copied.
- [AMD GPUOpen](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK): the
  official SDK used by the separate frame-generation probe.
- TEAM ASOBI / Sony Interactive Entertainment: ASTRO BOT and game assets/audio.
- fahziputraj: local gameplay, hardware testing and experiment direction.
- OpenAI Codex: AI-assisted research, implementation, test preparation and media
  drafting. Player feedback and automated test results are stated separately;
  comprehensive human code review and full game validation are not claimed.

This is an independent experiment, without an endorsement from the game,
hardware or emulator projects. No game content, keys or firmware are distributed
in this branch. Project code and third-party components retain their respective
licenses.
