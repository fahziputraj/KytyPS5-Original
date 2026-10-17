# Sub-project 1: port upstream input mapping UI (design)

Date: 2026-10-17. Branch: `feature/input-upstream-port`, cut from
`feature/global-pacing-fsr` at `995f7197`. Merging back requires the owner's
approval.

## Why

The owner reports that this fork's input mapping is much poorer than the
original Kyty's, and that gyro and touchpad are hard to use on controllers
other than a DualSense. Their controller is a Rexus Daxa Darion AX9. Its gyro
is only reported in Nintendo Switch Pro mode, which also costs a lot of CPU in
the emulator.

Upstream KytyPS5 improved controller setup and input mapping after this fork's
Jetsku int19.1 base (`aa3b2cd5`). None of that work is on this branch.

## Roadmap this belongs to

The owner approved four sub-projects, in order. Each gets its own spec, plan,
implementation and delivery to `E:\KytyPS5-Build`:

1. **This spec:** port upstream's controller setup and input mapping UI.
2. Cheaper NS Pro input and launcher input diagnostics. Measure first with
   `controller_probe` and the AX9 in each mode; then batch and coalesce SDL
   sensor events and add an optional motion sample cap.
3. Gyro sources (physical, right stick, mouse; hotkey switch), a persistent
   DualSense with motion as seen by the guest, the touchpad gesture scheme
   below, and stick/gyro tuning.
4. Per-game controller profiles and settings safety (backups, import/export,
   per-section reset, INI validation).

Decisions already made for later sub-projects, recorded so they are not lost:

- The owner uses the AX9 in **both** modes. NS Pro mode keeps real gyro and must
  get cheaper. XInput mode needs emulated gyro, because XInput's report has no
  motion fields (see `docs/CONTROLLER-MOTION.md`).
- The guest must always see one connected DualSense with motion, across
  physical disconnects and controller-mode changes. The owner has not observed
  the current failure modes yet, so verification happens on their PC.
- Touchpad scheme for pads without a touchpad, built around one mapped
  "touchpad" button (default Back/View):
  - tap = centre click;
  - hold + left stick left/right = left/right-half click;
  - hold + right-stick flick = swipe;
  - double-tap or hotkey = free-finger mode (right stick or mouse moves the
    finger, R2 presses), toggled off the same way;
  - in free-finger mode, hold L1 = mirrored second finger for pinch/rotate.

  All parts are remappable.
- Settings additions wanted: per-game controller profiles, stick and gyro
  tuning, input diagnostics, settings safety.

## Scope of sub-project 1

Cherry-pick these upstream commits in order with `git cherry-pick -x`,
keeping upstream authorship:

| Order | Upstream | Content |
| --- | --- | --- |
| 1 | `b65dc122` | Track pad port ownership and reject duplicate opens |
| 2 | `1cfb9522` | Back as the touchpad click on pads without a touchpad (#1355) |
| 3 | `7da3c4ed` | Controller setup screen (`ControllerPreview`, `DualSenseWidget`) and resizable game grid |
| 4 | `3ff6606f` | #1541: tabbed keyboard/controller mapping, gamepad remapping, emulated touchpad (mouse or gamepad; cursor, trail and particle visuals), right-stick gyro toggle, rumble from haptic audio on non-DualSense pads, `host_input` and updated `pad_haptics` tests |

Not ported, because they do not concern input: upstream's splash screen, HDR,
trophy toast/localization, DLC, network, updater, executable rename and
renderer/shader commits. Where #1541's overlay code sits beside splash or HDR
code that this branch lacks, adapt the touchpad drawing to this branch's
`systemOverlay.cpp` instead of importing those features.

`b65dc122` and `7da3c4ed` merge without conflicts (checked with `git merge-tree`).
`3ff6606f` conflicts in `CMakeLists.txt`, `systemOverlay.cpp`, `hostInput.cpp`,
`window.cpp`, `controller.cpp/.h`, `dualSenseHaptics.cpp` and the launcher
input files, because this fork changed the same areas.

## Conflict-resolution rules

1. **Physical motion stays this fork's.** Keep the motion-source selection
   (`--controller-motion active|off|<GUID>`), the motion history rate
   (`--controller-motion-rate`), sensor setup on the SDL event thread outside
   the guest pad lock, history ring behaviour and the fresh neutral report on
   source disconnect (`d4bd3ae4`, `2c059446`).
2. **Right-stick tilt (upstream "gyro emulation") is a second motion source.**
   While toggled on, it drives the guest's orientation and consumes the right
   stick axes, as upstream does, instead of the physical gyro. While off, the
   physical source (if any) is used. Its updates go through the same
   report/history publication path, so the rate cap applies to it too.
3. **Input routing follows upstream.** SDL gamepad buttons, triggers and left
   stick go through `HostInputPadButton`, `HostInputPadTrigger` and
   `HostInputGamepadAxis` with the Controller-tab bindings. Keep this fork's
   host-input pulse/minimum press, mouse-to-right-stick (F7), hide-cursor
   handling and controller-probe tooling.
4. **Default deviation from upstream.** Upstream's controller defaults bind
   `GyroToggle` to Back, which removes the only touchpad click on Xbox-style
   pads (the owner's AX9 in XInput mode). On this branch:
   - on a pad with no touchpad, Back produces a centred touchpad click (the
     `1cfb9522` behaviour), unless the owner binds Back to something else;
   - `GyroToggle` starts unbound and is chosen in the Controller tab.
5. **Rumble fallback** for non-DualSense pads is ported as is and scaled by this
   fork's existing vibration intensity setting.
6. **Touchpad visuals.** The cursor, trail and particle drawing are ported into
   this branch's overlay. The installed cursor images (`assets/cursors`) follow
   the CMake install rule, so the transactional installer picks them up from
   the install manifest.
7. **Compatibility.** Existing keyboard-only `host_input_mapping` lists load
   unchanged. Controller bindings are stored alongside them. Correction after the
   final review: rolling back is not lossless. A runtime from before the port stops
   with "Invalid input mapping" on the new entries, so the docs tell the owner to
   reset the mapping before restoring an older runtime. Original text: an older launcher
   ignores the new controller bindings; going back loses only those bindings.
8. **Unchanged:** the Astro shortcut and its fixed-30 Performance1080p profile
   with 60 Hz guest vblank, presentation/CPU/FSR/DLSS settings and status, the
   motion-source picker in Global settings > Controller, and the installer.

Code review during the port: parts of #1541 were written with Copilot. Review
the code touched while resolving conflicts. Fix real defects with a test and
list them in the delivery notes; leave working code that is merely stylistic.

## Testing

Automated, on the iGPU PC:

- Add `host_input` to the native delivery gate in `tools/update-build.ps1` and
  build its target in `tools/build-windows-local.ps1`. Keep `pad_haptics`,
  `controller_settings` and every existing gate test passing.
- New checks:
  - Back gives a centred touchpad click on a pad with no touchpad;
    `GyroToggle` defaults to unbound.
  - Right-stick tilt overrides the physical source only while enabled, and a
    disable returns to the physical source with a neutral, non-stale report.
  - An old keyboard-only `host_input_mapping` list loads unchanged; controller
    bindings round-trip through the INI.
- Developer CPU suite, installer fixtures (PowerShell 5.1 and 7) and Astro
  shortcut guard checks still pass.

Delivery: `tools/update-build.ps1 -Destination E:\KytyPS5-Build -Fsr3Probe
-SkipFsr3GpuTest -DlssPrototype -Jobs 3` from the new branch, then the
independent verification used before:
- the manifest commit equals HEAD, with `modified: false`;
- binary hashes match and staged files equal the stage;
- pre-existing user and handoff files are byte-identical;
- the cursor assets are installed;
- emulator `--help` and an 8-second launcher start with a separate INI succeed.

Cannot be verified here (no controller connected; the renderer does not run on
UHD 730). The owner's home-PC checklist:

1. The controller setup screen and both mapping tabs open. A controller binding
   saves and survives a launcher restart.
2. AX9 in XInput mode: Back clicks the touchpad in a game that needs it; a
   remapped button works; rumble works.
3. AX9 in NS Pro mode: the physical gyro still moves the camera with
   `--controller-motion active`. A bound gyro toggle switches to right-stick
   tilt and back.
4. Mouse touchpad mode and the gamepad touchpad mode work, and their cursor
   visuals appear and disappear with the toggles.
5. Astro 01.007 fixed-30 shortcut behaves as before.

## Out of scope for sub-project 1

The NS Pro CPU work, gyro-from-mouse, the persistent virtual DualSense, the new
touchpad gestures, stick/gyro tuning, per-game profiles and settings safety.
They belong to sub-projects 2-4.
