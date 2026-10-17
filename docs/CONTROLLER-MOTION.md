# Controller motion and input overhead

Motion source selection is independent of the button/stick controller. In the
launcher's **Global settings > Controller**, press **Detect** to see which
connected SDL gamepads expose a gyroscope or accelerometer. Choose **Active
gamepad (native SDL)** for normal use, a detected physical motion device when a
separate virtual Xbox pad supplies buttons, or **Off** for a controlled CPU
comparison. Detection is on demand and does not run a background scan.

This does not create gyro support in a controller mode that hides the sensor.
[XInput's gamepad state](https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad)
contains buttons, triggers and sticks, without motion fields.
[SDL reports the sensor capabilities exposed by its driver](https://wiki.libsdl.org/SDL3/SDL_GamepadHasSensor).
A controller can contain a physical gyro while exposing none in Xbox 360 mode.
If firmware stops sending motion reports in that mode, use its supported
motion-capable mode, or a separate provider that exposes actual motion data.
No device-specific raw HID protocol or network motion provider is guessed here.

An explicit source is saved by SDL GUID rather than a temporary session ID.
If two sensor devices share that GUID, selection fails closed; select the active
gamepad or disconnect the ambiguous duplicate. A disappeared source clears its
last motion values instead of keeping stale rotation.

## What changes in the input path

- Sensor setup runs on the SDL event thread, outside the lock used by guest pad
  reads. Only the selected source has its supported sensors enabled. Disabling
  motion through the guest API also disables that source's SDL sensor reporting.
- Motion history defaults to **250 Hz**, with 125/500/1000 Hz and raw options.
  Every valid gyro sample still contributes to orientation using its original
  timestamp. The setting bounds queued reports rather than slowing down the
  game's clock, limiting sticks/buttons or changing angular-velocity units.
- Closely spaced motion updates replace an unread motion snapshot; they do not
  replace intervening button edges. Accelerometer and gyro updates sharing a
  timestamp can form one history report. `PadReadState` always sees the latest
  integrated pose, including between history publications.
- Pad reads consume unread history in order. Empty reads no longer scan the
  previously consumed 64-entry ring or acquire a second lock for the fallback
  snapshot. Overflow still keeps the newest 64 reports.

These changes remove identifiable extra work. They do not establish that input
caused a particular game's slowdown or promise a measured FPS gain.

Automated checks cover a one-second 90-degree rotation at 60/200/1000 sensor
samples per second, output units, history overflow/partial reads, quick button
edges, invalid samples, recentering, separate button/motion sources and device
removal. A reentrant read during sensor setup checks that hardware commands do
not hold the guest pad lock. Launcher tests cover settings persistence and
safe defaults for old or malformed configuration.

## Diagnostics and comparison

Build `controller_probe` for a report of connected gamepads and sensor
capabilities. The local Windows build helper also copies it beside the emulator.
It does not print controller serial numbers, device paths or Bluetooth addresses.

```powershell
.\controller_probe.exe
.\controller_probe.exe --sample-seconds 5
.\controller_probe.exe --sample-seconds 5 --motion
```

Compare the same physical connection and controller mode with motion off/on.
The JSON reports process CPU seconds, elapsed time and sensor event counts for
SDL input only. CPU seconds divided by wall seconds is a fraction of one CPU
core, not the overall CPU usage of the game. Startup/device initialization is
excluded. The tool does not benchmark guest input processing or graphics.

The current test machine's Xbox 360 presentation exposed no gyro or
accelerometer and produced zero sensor events in both five-second probes.
This establishes what that mode exposed at test time; it does not identify the
controller's manufacturer or rule out another supported hardware mode.

For game comparisons, leave the camera still in the same scene and compare raw,
250 Hz and 125 Hz history. Keep graphics/timing settings, cache state and
background load constant. Verify actual gyro movement, button taps and timing,
not just the FPS counter.

CLI equivalents:

```text
--controller-motion active
--controller-motion off
--controller-motion <32-lowercase-hex-SDL-GUID>
--controller-motion-rate 250
```

The local Astro wrapper accepts `-MotionSource active|off|GUID` and
`-MotionRate 0|60..1000`. Its fixed-30 and optional adaptive clocks are unchanged.
These source changes are newer than Windows pacing preview 1; its immutable
release tag does not contain this controller work.
