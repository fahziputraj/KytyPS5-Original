# Port Upstream Input Mapping UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring upstream KytyPS5's controller setup and input mapping work onto this fork,
keeping this fork's motion sources authoritative. Back stays the touchpad click on pads
without a touchpad, and right-stick tilt becomes a switchable second motion source.

**Architecture:** Cherry-pick four upstream commits in order, keeping their authorship
(`-x`). The first three apply cleanly. The fourth (#1541) needs conflict resolution
following fixed per-file rules. Then two small test-first commits implement the spec's
deviations from upstream. Delivery uses the existing transactional updater.

**Tech Stack:** C++20 (clang-cl 22.1.3, IPO/PGO), SDL 3.4.16, Qt 6.10.3 launcher, CMake/Ninja,
CTest, PowerShell 5.1/7 build scripts.

**Spec:** `docs/superpowers/specs/2026-10-17-input-upstream-port-design.md`

## Global Constraints

- Work on branch `feature/input-upstream-port`. No push, PR or merge into `feature/global-pacing-fsr` without the owner.
- Cherry-picks use `git cherry-pick -x` and keep the upstream author. A conflict-resolved
  pick appends a `Conflict resolution:` paragraph and
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Every new commit ends with that
  line.
- Do not change `tools/Start-AstroBot*`, `patches/`, presentation/CPU/FSR/DLSS code, the
  installer, or the fixed-30 Performance1080p profile (60 Hz guest vblank).
- Never edit `3rdparty/SDL3` (its contributor policy excludes AI-written changes).
- Physical motion stays this fork's: `--controller-motion active|off|<GUID>`,
  `--controller-motion-rate`, sensor setup on the SDL event thread outside the pad lock,
  `AddMotionState` publication, neutral report on source disconnect.
- Format only changed lines with `src/.clang-format`. Stage first, then run
  `python -I _Build/deps/windows-toolchain/LLVM/bin/git-clang-format --binary _Build/deps/windows-toolchain/LLVM/bin/clang-format.exe --style file:src/.clang-format`.
- **BUILD** (from the repo root, PowerShell):
  `powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\build-windows-local.ps1' -Targets <comma list> -Jobs 4"`.
  Use `-Command`, not `-File`, or the target list binds as one string.
- **CTEST** (PowerShell):
  `$t = Get-Content _Build\deps\windows-toolchain\toolchain.json -Raw | ConvertFrom-Json; $env:PATH = "$PWD\_Build\windows-int19\launcher;$($t.QtRoot)\bin;$env:PATH"; & (Join-Path (Split-Path $t.Cmake) 'ctest.exe') --test-dir _Build\windows-int19 --output-on-failure -R '<regex>'`.
- **INPUT TARGETS:** `launcher,kyty_emulator,controller_settings_tests,launcher_presentation_config_tests,pad_haptics_tests`
  (from Task 2: plus `host_input_tests`).
- **INPUT TESTS regex:** `^(controller_settings|launcher_presentation_config|pad_haptics|host_input)$`
  (`host_input` exists from Task 2).
- Deliver only with
  `./tools/update-build.ps1 -Destination E:\KytyPS5-Build -Fsr3Probe -SkipFsr3GpuTest -DlssPrototype -Jobs 3`
  from a clean tree. It preserves saves, settings and caches.
- Spec refinement found during planning: this fork already has rumble from haptic audio
  for non-DualSense pads (`c03593ef`, `b090e403`, with per-channel gains and the vibration
  intensity). Keep it, and add only upstream's stale-device reset.

## Review Focus

1. **Two pads, one with a touchpad.** Back must follow the touchpad presence of the pad
   that pressed it, not the active pad (test in Task 3).
2. **Game disabled motion.** With `PadSetMotionSensorState(false)`, toggling right-stick tilt
   still consumes the stick but must keep the reported pose neutral (test in Task 4).
3. **Source unplugged during tilt.** With tilt on, unplugging the physical motion source
   and then turning tilt off must publish a neutral, non-stale report (test in Task 4).
4. **Old saved mappings.** A fork-era list with mouse bindings and sensitivity
   (`Cross=Mouse:Left`, `MouseSensitivity=2.0`) must load exactly as before, with gamepad
   defaults intact (test in Task 2).
5. **Disabled controller tab.** With `Pad=Disabled`, Back must do nothing at all, not click
   the touchpad (test in Task 3).

---

### Task 1: Apply the three clean upstream commits

**Files:** from upstream: `src/libs/controller.cpp/.h`, `src/libs/libPad.cpp` (`b65dc122`);
`src/graphics/presentation/window/window.cpp` (`1cfb9522`); launcher setup screen and grid
(`7da3c4ed`): `controllerPreview.*`, `dualsenseWidget.*`, `inputMappingDialog.*`,
`configurationListWidget.*`, `mainDialog.cpp`, forms, icons, `resources.qrc`,
`launcher.qss`, `CMakeLists.txt`.

**Interfaces:** Produces upstream's `ControllerPreview` and `DualSenseWidget`, and
`InputMappingDialog(const QStringList& mapping, ControllerPreview* preview, QWidget* parent)`.
Task 2 relies on them.

- [ ] **Step 1: Cherry-pick in order**

Run: `git cherry-pick -x b65dc122 1cfb9522 7da3c4ed`.
Expected: three commits. If `1cfb9522` conflicts in `window.cpp`, keep this fork's sensor
event handling and add upstream's Back hunk inside `GameEventController`, then
`git cherry-pick --continue`.

- [ ] **Step 2: Build INPUT TARGETS (Task 1 list)**

Expected: build exit 0 with no new warnings in the cherry-picked files.

- [ ] **Step 3: Run INPUT TESTS with regex `^(controller_settings|launcher_presentation_config|pad_haptics)$`**

Expected: 3/3 pass. If a fork test fails because of upstream behaviour, stop and report.
Do not edit the tests to pass.

### Task 2: Cherry-pick #1541 with conflict resolution and wire its tests into the gate

**Files:**
- Modify (conflicts): `CMakeLists.txt`, `src/graphics/presentation/window/hostInput.cpp/.h`,
  `window.cpp`, `src/graphics/presentation/systemOverlay.cpp`, `src/libs/controller.cpp/.h`,
  `src/libs/dualSenseHaptics.cpp`, `src/launcher/src/inputMappingDialog.cpp`,
  `src/launcher/include/inputMappingDialog.h`, `src/launcher/src/controllerPreview.cpp`,
  `src/launcher/include/controllerPreview.h`
- Add (upstream): `assets/cursors/hand.png`, `assets/cursors/drawing.png`, `tests/HostInputTests.cpp`
- Modify: `tools/build-windows-local.ps1` (default `-Targets` adds `host_input_tests`, `pad_haptics_tests`),
  `tools/update-build.ps1` (gate regex adds `host_input|pad_haptics`)
- Test: `tests/HostInputTests.cpp`, `tests/LauncherPresentationConfigTests.cpp`

**Interfaces:**
- Consumes: Task 1's launcher classes.
- Produces (upstream names, kept):
  - `HostInputPadButton(int id, int sdl_button, bool down)`,
    `HostInputPadTrigger(int id, bool left, int value)`,
    `HostInputGamepadAxis(int id, bool horizontal, int value) -> bool`,
    `HostInputDisconnectGamepad(int id)`;
  - `class InputMap` with `PadControl(std::size_t source)` and
    `static SourceFromButton(int)`; `CONTROL_INFO`, `PAD_SOURCES`,
    `INVALID_CONTROL`/`INVALID_SOURCE`, `ControlFromName`;
  - `Controller::ToggleGyroEmulation(int id)` and
    `GameController::UpdateEmulatedTilt()`, with members `m_gyro_emulation`,
    `m_emulated_stick`, `m_emulated_time`.

- [ ] **Step 1: Start the pick**

Run: `git cherry-pick -x 3ff6606f`. Expected: conflicts in the files above.

- [ ] **Step 2: Resolve each file by these rules**

| File | Keep from upstream | Keep from this fork |
| --- | --- | --- |
| `hostInput.cpp/.h` | Whole #1541 version: `InputMap` with `Keyboard=Disabled`/`Pad=Disabled`/`Pad:` entries, gamepad and mouse touchpad modes, visual state | `HostInputPulse g_input_pulse` and `ApplyHostKey`, `KYTY_HOST_INPUT_MIN_PRESS_MS`, `Common::HostInputTrace` calls, pulse-aware `HostInputWaitEvent` timeout and the `EXIT` on a failed wait (the fork delta from `aa3b2cd5` is about 44 lines) |
| `window.cpp` | Button/trigger/left-stick routing through `HostInputPad*`; mouse touchpad hooks; `HostInputSetTouchPadMode(false)` on mouse leave; disconnect hook | `SDL_EVENT_GAMEPAD_SENSOR_UPDATE` with `sensor_timestamp`, `Controller::UpdateHostMotion()` in `Run()`, resize/minimize swapchain fixes |
| `controller.cpp/.h` | `ToggleGyroEmulation`, `UpdateEmulatedTilt`, emulated-stick handling in `Axis`/`RightStick` (unchanged here; Task 4 changes them) | Motion devices/source selection, `Motion()`, `AddState(bool)`/`AddMotionState`, history ring, neutral report on disconnect, out-of-lock sensor setup |
| `dualSenseHaptics.cpp` | Only `CloseDevice(stream); stream->next_check = 0;` in the non-PS5 branch | This fork's rumble fallback and route diagnostics |
| `systemOverlay.cpp` | Only the touchpad cursor/trail/particle drawing and its `GetHostInputTouchPadVisualState()` use | Everything else; do not import splash, HDR or trophy-toast code |
| `CMakeLists.txt` | `host_input_tests` target, its `add_test(NAME host_input ...)` and test-list entry, `kyty_touch_cursors` copy target and `install(DIRECTORY .../assets/cursors DESTINATION assets)` | All fork targets and options |
| Launcher input files | #1541 versions | Our includes/callers if signatures differ |

If `HostInputTests.cpp` (which `#include`s `hostInput.cpp`) calls SDL functions that only
the fork code uses, add fakes in its `Fake` namespace following the existing `#define`
pattern. Do not stub production code.

- [ ] **Step 3: Add compatibility tests**

In `tests/HostInputTests.cpp`, add `TestForkKeymapStillLoads()` and call it from `main()`:

```cpp
options.keymap = {"Cross=J", "Square=Mouse:Left", "MouseSensitivity=2.0"};
Config::Load(options);
const InputMap old;
Check(old.Custom() && old.FindKey(SDLK_J) == ControlFromName("Cross") &&
          old.FindMouseButton(SDL_BUTTON_LEFT) == ControlFromName("Square") &&
          old.MouseSensitivity() == 2.0f &&
          old.PadControl(InputMap::SourceFromButton(SDL_GAMEPAD_BUTTON_SOUTH)) == ControlFromName("Cross"),
      "a fork-era keyboard mapping lost a binding, the sensitivity or the gamepad defaults");
```

In `tests/LauncherPresentationConfigTests.cpp`, check that a `host_input_mapping` of
`{"Cross=J", "Pad:Circle=South", "Pad=Disabled", "MouseSensitivity=2.0"}` survives
`WriteSettings`/`ReadSettings` and `CopyEmulatorSettingsFrom` element for element, in
order.

- [ ] **Step 4: Wire the gate**

Add `host_input_tests` and `pad_haptics_tests` to the default `-Targets` in
`tools/build-windows-local.ps1`. Add `host_input|pad_haptics` to `$taskTestPattern` in
`tools/update-build.ps1`.

- [ ] **Step 5: Build INPUT TARGETS (with `host_input_tests`) and run INPUT TESTS**

Expected: build exit 0; 4/4 pass.

- [ ] **Step 6: Review the resolved code**

Read the conflict-resolved functions. For each real defect, add a failing test in
`HostInputTests`/`ControllerSettingsTests`, then the fix, and list it in the commit message.
Leave stylistic code alone.

- [ ] **Step 7: Finish the pick**

Stage, format the changed lines, then run `git cherry-pick --continue`. Edit the message to
add a `Conflict resolution:` paragraph summarizing the table rows used, plus the
Co-Authored-By line.

### Task 3: Shared pad defaults, Back as touchpad click, gyro toggle unbound

**Files:**
- Create: `src/common/padInputDefaults.h`
- Modify: `src/graphics/presentation/window/hostInput.cpp` (use the shared table; add
  `InputMap::PadDisabled()`, `BackClicksTouchPad`, and the Back path in `HostInputPadButton`)
- Modify: `src/launcher/src/inputMappingDialog.cpp` (pad defaults from the shared table;
  `GyroToggle` has no pad default)
- Test: `tests/HostInputTests.cpp`, `tests/LauncherPresentationConfigTests.cpp`

**Interfaces:**
- Produces:
  - `namespace Common { struct PadDefaultBinding { std::string_view control, source; }; inline constexpr std::array PAD_DEFAULT_BINDINGS = {...}; }`.
    The pairs are upstream's `PAD_DEFAULTS` without `{"GyroToggle","Back"}`.
  - `bool InputMap::PadDisabled() const`: true only for an explicit `Pad=Disabled` entry.
  - `bool BackClicksTouchPad(std::size_t source, const InputMap& map, bool pad_has_touchpad)`.
    It returns true when `source` is Back, `map.PadControl(source) == INVALID_CONTROL`,
    `!map.PadDisabled()` and `!pad_has_touchpad`.

- [ ] **Step 1: Write failing tests**

In `HostInputTests.cpp`:
- Add a fake `SDL_GetNumGamepadTouchpads`: pad 1 has 0 touchpads, pad 2 has 1.
- Extend the fake `SetTouchPad` to record finger 0's down state and x as `guest_touch0`
  and `guest_touch0_x`.
- Add `TestBackClicksTouchPadWithoutOne()` with these cases:
  - Defaults, pad 1, Back down: `guest_buttons & PAD_BUTTON_TOUCH_PAD`, `guest_touch0`,
    `guest_touch0_x == 0.5f`. Back up clears both.
  - Defaults, pad 2, Back down: no touchpad button and no touch.
  - Keymap `{"Pad:Cross=Back"}`, pad 1, Back down: `guest_buttons == PAD_BUTTON_CROSS`
    and no touch.
  - Keymap `{"Pad=Disabled"}`, pad 1, Back down: nothing changes.
  - Defaults: `PadControl(SourceFromButton(SDL_GAMEPAD_BUTTON_BACK)) == INVALID_CONTROL`.

In `LauncherPresentationConfigTests.cpp`, `#include "common/padInputDefaults.h"` and check
that no `Common::PAD_DEFAULT_BINDINGS` entry has control `GyroToggle`, and that `TouchPad`
maps to `Touchpad`.

- [ ] **Step 2: Build and run INPUT TESTS; verify the new checks fail**

Expected: FAIL with "Back did not click the touchpad", or a compile error for the missing
header.

- [ ] **Step 3: Implement**

- Add the header.
- Replace `PAD_DEFAULTS` in `hostInput.cpp` and the launcher's pad default column with
  `Common::PAD_DEFAULT_BINDINGS`.
- In `HostInputPadButton`, before `ApplyPadControl`, check
  `BackClicksTouchPad(source, map, SDL_GetNumGamepadTouchpads(SDL_GetGamepadFromID(id)) > 0)`.
  When true, send `Controller::SetTouchPad(id, 0, down, 0.5f, 0.5f)` and
  `Controller::SetButton(id, PAD_BUTTON_TOUCH_PAD, down)`, in the press/release order
  `1cfb9522` used, and return.

- [ ] **Step 4: Build and run INPUT TESTS**

Expected: 4/4 pass.

- [ ] **Step 5: Commit** `feat(input): keep Back as the touchpad click on pads without one`

### Task 4: Right-stick tilt as a switchable second motion source

**Files:** Modify `src/libs/controller.cpp`. Test: `tests/ControllerSettingsTests.cpp`.

**Interfaces:**
- Consumes: Task 2's `ToggleGyroEmulation`, `UpdateEmulatedTilt` and `m_emulated_*`;
  the fork's `Motion`, `AddMotionState`, `SetMotionSensorState`, `Disconnect`.
- Produces the behaviour:
  - Tilt may be enabled even when the active pad has a physical gyro; upstream's
    SDL sensor query under `m_mutex` is removed.
  - While tilt is on, `Motion()` ignores physical samples.
  - `UpdateEmulatedTilt` publishes through `AddMotionState(now)` and keeps the pose
    neutral while `m_motion_enabled` is false.
  - Turning tilt off resets `orientation {0,0,0,1}`, `accel {0,1,0}`, `gyro {}`,
    `m_gyro_time`, `m_up_reference_valid` and `m_motion_report_time`, restores the
    right-stick axes, and calls `AddState(true)`.

- [ ] **Step 1: Write failing tests in `ControllerSettingsTests.cpp`**

Add `TestEmulatedTiltOverridesPhysicalMotion()`, called from `main()`. With pad 1
motion-capable and source `active`:
- Toggling tilt on succeeds even though pad 1 has a gyro: a full right-stick deflection
  (255,128) tilts the reported orientation by about 45 degrees.
- A physical gyro sample while tilt is on leaves the orientation unchanged.
- Ten stick updates inside one `--controller-motion-rate 250` interval add at most one
  motion history entry.
- With `PadSetMotionSensorState(handle, false)`, a stick deflection keeps the pose
  identity (Review Focus 2).
- Toggling off reports identity orientation, accel (0,1,0), zero gyro and the stick's
  current axes.
- The next physical sample 50 ms later integrates only that sample's rotation, not the
  time spent in tilt mode.
- Disconnecting pad 1 while tilt is on, then toggling off, publishes a neutral report
  (Review Focus 3).

- [ ] **Step 2: Build `controller_settings_tests`; run `^controller_settings$`**

Expected: FAIL. The first assertion fails because upstream refuses tilt on a gyro pad.

- [ ] **Step 3: Implement the behaviour listed under Interfaces in `controller.cpp`**

- [ ] **Step 4: Build INPUT TARGETS; run INPUT TESTS**

Expected: 4/4 pass.

- [ ] **Step 5: Commit** `feat(input): make right-stick tilt a switchable second motion source`

### Task 5: Documentation, delivery and verification

**Files:** Modify `docs/CONTROLLER-MOTION.md`.

- [ ] **Step 1: Document**

Add a section "Input mapping and controller setup (upstream port)" covering:
- the Keyboard and Controller tabs;
- Back as the touchpad click on pads without one, and how to rebind it;
- `GyroToggle` unbound by default;
- tilt precedence over the physical gyro, and that tilt respects the game's motion
  enable;
- the touchpad modes (F6 mouse mode, gamepad touchpad mode);
- that the fork's own rumble fallback is kept.

Also add the spec's five-item home-PC checklist verbatim, labelled "not verified on the
development PC". Commit `docs(input): describe the ported mapping UI and home checks`.

- [ ] **Step 2: Run the suites the native gate does not cover**

Run `./tools/test-temporal.ps1 -FsrSdkRoot "$PWD/_Build/deps/fidelityfx-1.1.4/sdk-package" -DlssSdkRoot "$PWD/_Build/deps/dlss-310.9.16"`.
Expected: 8/8 pass.

Run `pwsh -NoProfile -File tools/test-runtime-install.ps1` and
`pwsh -NoProfile -File tools/test-astro-launcher.ps1`.
Expected: 71 and 30 checks pass. The delivery gate covers PowerShell 5.1.

- [ ] **Step 3: Run the full gate and deliver from a clean tree**

Run the delivery command from Global Constraints.
Expected: exit 0; the log shows `host_input` and `pad_haptics` passing, 71 installer
checks and 30 Astro shortcut checks.

- [ ] **Step 4: Verify the installed runtime independently**

Check:
- `BUILD-INFO.json` commit equals `git rev-parse HEAD`, with `modified: false`;
- emulator and launcher hashes match;
- every file in `_Build/runtime-int19.files.txt` matches the stage;
- `assets\cursors\hand.png` and `assets\cursors\drawing.png` are installed;
- pre-existing user files (`u59-preset.json`, `_Patches\PPSA21564.json`, `Claude-Handoff*`)
  are byte-identical to a snapshot taken before delivery;
- no journal is left;
- `kyty_emulator.exe --help` exits 0;
- the launcher stays up for 8 s with `_Build\runtime-smoke` as its working directory,
  and the test INI is unchanged.
