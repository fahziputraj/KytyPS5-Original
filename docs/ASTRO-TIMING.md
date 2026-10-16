# Astro Bot: variable presentation and CPU placement

The base is Jetsku's exact `u59-windows-20261013-int19.1` tag, commit
`28bc20f146a276479acf42d2e405fb46a4491b8e`. Local delivery tools were rebased onto
it. The earlier official-base crash/preload experiments are preserved on
`codex/backup-pre-jetsku-20261017` (`a4bc67a1`); their replacement shader cache and
resource patches were not replayed over Jetsku's existing implementations.

The screenshots show an approximately 60-to-33 FPS camera-dependent drop in Sky
Garden. The lower GPU utilization and higher CPU utilization suggest a CPU limit;
they do not identify an engine render-distance setting. Shader preloading reduces
first-use compilation stalls, not the ongoing cost of a larger visible scene.

## Timing

`KYTY_PRESENT_WHEN_READY=1` uses two service threads. One advances guest vblank
events from elapsed monotonic host time at the configured frequency (60 Hz for
Astro). The other presents completed flip groups as soon as their frame interval
allows. It sleeps on readiness/vblank notifications rather than polling in a
spin loop. Vulkan acquire, presentation, and GPU waits never run on the guest
vblank thread. Port locks are released across host presentation, with generation
checks before delivering flip completion after a close.

Missed clock ticks are reflected in the next vblank count and coalesced event;
they do not create a burst of render work. Pause/resume rebases the clock without
a catch-up backlog. Guest clock APIs and the TSC frequency remain unchanged.
The title's own flip interval still applies. `KYTY_PRESENT_FPS=30` optionally
limits presentation while retaining a 60 Hz guest display clock; `0` leaves the
limit at the title's configured refresh/flip interval.

Hardware VRR/G-SYNC is a separate display/driver facility. This change permits
variable frame delivery; it does not enable a monitor's VRR or claim to convert
fixed-step game logic into a variable-step simulation. A game that advances by
one fixed simulation step per rendered frame can still need a title-specific
patch. Gameplay-speed validation must use elapsed time or a repeatable animation,
not only an FPS counter or the number of vblank events.

`KYTY_PRESENT_TIMING_LOG=1` prints five-second samples of guest vblank rate and
completed guest flips. These count emulator timing and presentation, not the
game's internal simulation steps or generated frames. `KYTY_PRESENT_WHEN_READY=0`
keeps the original fixed presentation path for A/B testing.

Related primary sources reviewed:

- [Sky Garden camera-direction report](https://github.com/Jetsku/KytyPS5/issues/28).
- [chenxiao07's separation of vblank and host presentation](https://github.com/chenxiao07/KytyPS5/commit/eadf554fbd49a2cc4119b374a926ad3c01128aa7).
- [Earlier ready-frame presentation experiment](https://github.com/RainKikyou/KytyPS5/commit/aac0f9f5e7c5cd31ce5e0aeddd428147584017a6).
- [Vulkan presentation modes](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html).

The implementation retains int19.1's multi-port flip groups. It does not copy
the other fork's single-port, frame-generation or display-clock dependencies.

## CPU placement and delivery

The existing local hybrid CPU change (`63a7cd62`, replayed as `9f8e2fa8`) chooses
reserved CP cores from the fastest core class. With `KYTY_CPU_HYBRID=on`, critical
CP helper threads use the remaining P-cores and background compilation/cache
workers use E-cores. Guest workers retain the general CPU pool. Homogeneous CPUs
keep their existing placement. This is a scheduling change, not a render-distance
hack or a claim of guaranteed FPS improvement.

`tools/build-windows-local.ps1` uses LLVM 22.1.3, release IPO and the bundled
int19 Sky Garden PGO profile. Anonymous function names require the profile's
exact `C:\kyty-src19` path. The build temporarily redirects that junction to this
checkout and restores its previous target in `finally`; it refuses to do so if
another native build is running. Neither source checkout is moved or deleted.
The build output is `_Build/windows-int19`.

`tools/update-build.ps1` runs timing/CPU placement checks, stages the runtime,
backs up files before overwriting `F:\KytyPS5-Build`, and preserves the installed
INI, caches and saves. The launch wrapper selects variable presentation by
default; `-Timing Fixed` selects the comparison path and `-Fps 30` selects the
optional presentation cap. Normal play uses silent guest logging.

## Validation

Record the completed build, tests, launch results and same-scene timing checks
here before claiming a gameplay-speed or FPS improvement.
