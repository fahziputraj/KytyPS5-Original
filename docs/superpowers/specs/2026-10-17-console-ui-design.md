# Sub-projects 5 and 6: console-style launcher and in-game overlay (design)

Date: 2026-10-17. Builds on sub-project 1 (`feature/input-upstream-port`), which
brings upstream's game grid and controller screens into this fork.

## Why

The owner wants an eye-catching launcher in the style of a game-console home
screen ("like PS console"), and more useful information in the game window than
the current title bar gives (build, title, GPU, CPU, frame count and FPS). They
also asked for a revamp of the in-game overlay. Mockups were reviewed in the
conversation; the owner approved:

- **Home screen = direction A, "hero row".** The selected game's art fills the
  background. A row of rounded game tiles sits at the top; the focused tile is
  larger with a white focus ring. Below it: the title, chips (title ID/version,
  clock profile, active controller), a white **Play** button, **Game settings**
  and **Trophies**, then info cards (last played/playtime, compatibility,
  shader-cache state).
- **Full library = direction B, "library hub".** A left navigation rail (Home,
  Library, Controllers, Trophies, Settings), a search field, a large "continue"
  card for the last-played game and a cover grid.
- **Settings.** A category rail (Display, Performance, Controller, Audio,
  Upscaling, CPU, Advanced), search across all settings, a "Per-game · N changed"
  badge, each value showing whether it is inherited or set here, and plain-language
  hints from what was detected (for example "Rexus AX9 (XInput): no gyro in this
  mode; tilt or NS Pro mode gives motion").
- **In-game information overlay.** A full panel or a compact corner counter,
  toggled with a hotkey. Rows: game FPS, displayed FPS, frame time with 1% low,
  emulator CPU, VRAM, shaders compiling, input/gyro source, FSR state.

## Shared visual system

The look is inspired by console home screens. It uses no Sony logos, wordmarks,
fonts or trademarked artwork. Controller hints use the connected pad's own
labels, drawn from this project's SVGs: A/B on Xbox-style pads, face-button
names on a DualSense.

| Token | Value | Use |
| --- | --- | --- |
| `bg` | `#0A1020` | Page background |
| `rail` | `#070C18` | Navigation rail, overlay panels |
| `surface-1` | `#0F1A33` | Cards, search field |
| `surface-2` | `#16223D` | Selected rows, chips, secondary buttons |
| `hero-base` / `hero-accent` | `#132A52` / `#1D3F78` | Backdrop when no game art is available |
| `accent` | `#3B82F6` | Progress, links, active indicators |
| `text-primary` | `#E6ECF5` | Body text |
| `text-secondary` | `#B7C6E3` | Supporting text |
| `text-muted` | `#7D8DB0` | Placeholders, captions |
| `success` / `warning` / `danger` | `#86EFAC` / `#FBBF24` / `#F87171` | Status, always with text |
| `focus` | `#FFFFFF`, 2 px ring, 3 px offset | Controller and keyboard focus |
| Primary button | White fill, `#0A1020` text | One per screen |

The token values are this project's own choice: the design dataset had no
console palette, and its "Dark Mode (OLED)" style with midnight-blue surfaces was
used as the base.

- **Type.** DM Sans (SIL Open Font License 1.1) is bundled in the launcher
  resources with its license, falling back to Segoe UI. It is downloaded once
  during implementation with a pinned SHA-256, after the owner approves the
  download. The overlay loads the same TTF into ImGui. Scale: 13 / 15 / 18 /
  24 / 32 px; weights 400 and 500.
- **Shape and spacing.** Radius 10 px for tiles and 12 px for cards, pills
  fully rounded. 4/8 px spacing scale.
- **Motion.** Focus moves animate tile scale 1.0 to 1.08 and the ring over
  150-200 ms, ease-out. Screen changes cross-fade in 200 ms. A "Reduce motion"
  setting turns all of it off. Animation never blocks input.
- **Accessibility.** Normal text needs 4.5:1 contrast and the focus ring 3:1 on
  every surface it can sit on; a unit test checks the token pairs. Every action
  works by keyboard, mouse and controller, and icon-only buttons have accessible
  names.

## Approaches considered (launcher)

1. **Qt Widgets with custom-painted widgets and delegates (chosen).** No new
   dependency: the installed Qt 6.10.3 package and the build deploy Widgets and
   Svg only. Fits the existing launcher code, its settings logic and its tests.
   Animation uses `QPropertyAnimation`/`QVariantAnimation` on painted properties.
2. **Qt Quick/QML.** Smoother scene-graph animation, but Qt Quick/QML are not in
   the installed package or the deployment script. It would mean a new
   toolchain dependency, new deployed plugins and a rewrite of every dialog.
3. **Embedded web view.** Heaviest runtime and a large new dependency.
   Rejected.

## Part A: launcher redesign (sub-project 5)

### Structure

`MainDialog` hosts a `QStackedWidget` of full-window pages. Upstream's
`ConfigurationListWidget` and the settings and mapping dialogs keep their data
logic and are re-hosted rather than rewritten.

| Page | Contents |
| --- | --- |
| `HomePage` (new) | Hero backdrop, tile row, title block, actions, info cards |
| `LibraryPage` (new) | Rail, search, continue card, cover grid (upstream's grid view with a new delegate) |
| `SettingsPage` (new shell) | Category rail and search around the existing settings sections, for global or per-game settings |
| `ControllersPage` | Upstream's controller setup and the mapping tabs, restyled |
| `TrophiesPage` | Existing trophy viewer, restyled |

A **Classic layout** switch in Settings keeps today's layout available. It
defaults to the new layout, and switching does not touch any game setting.

### Game art and data

- **Tiles and covers** use `sce_sys/icon0.png` through the existing
  `GameContent::ReadFile`, so archives work too.
- **Hero backdrop** uses `sce_sys/pic0.png`, then `sce_sys/pic1.png` if present.
  Otherwise it uses a flat backdrop from the icon's dominant colour, darkened so
  `text-primary` keeps 4.5:1 over it. Images are read and scaled off the UI
  thread and cached per title and size.
- **Chips** come from data the launcher already has: title ID and version, the
  selected clock profile, and the controller from the existing detection. A chip
  without data is hidden, never shown empty.
- **Last played and playtime (new).** The launcher records the emulator process
  start and exit per title in `Kyty.ini` under `[Playtime]`
  (`<TITLE_ID>\last_played`, `<TITLE_ID>\seconds`). A crash or kill still counts
  until the exit is observed. No other settings change.
- **Compatibility** comes from the existing `compatibilityDatabase`.
- **Shader-cache state** shows whether `_PipelineCache/<TITLE_ID>.*` files
  exist, and their size. The launcher only reads them, never writes.

### Navigation and input

- **Focus model.** A pure `FocusGrid` class handles row and grid movement,
  wrapping, page memory and "back" history. It is unit-tested without widgets.
- **Keyboard.** Arrows move, Enter activates, Esc goes back, Tab cycles regions.
- **Controller (new in the launcher).** The launcher already links SDL3. It
  polls gamepad events only while its window is active and no game is running,
  so launcher input never competes with a game.
  - D-pad or left stick: move.
  - South/Cross: activate.
  - East/Circle: back.
  - Start/Options: game settings.
  - Shoulders: switch rail section.
- **Mouse.** Hover and click with the same focus visuals.
- **Search** filters by title, title ID and version. In Settings it filters
  setting labels across categories and opens the matching category.

### Settings page behaviour

- Each row shows its value and origin: "Inherited" or "Set for this game".
  "Reset to global" clears a per-game override. The badge counts per-game
  overrides.
- **Hints** come from existing detection: controller type and sensors from the
  motion-source probe, and the GPU/driver status the launcher already reports.
  A hint never claims a capability that wasn't detected.
- Existing validation, import/export and the FSR/DLSS status text stay as they
  are. Sub-project 4's profiles and settings safety will reuse this page.

## Part B: in-game information and overlay revamp (sub-project 6)

### Data, always measured (never estimated)

| Row | Source | Note |
| --- | --- | --- |
| Game FPS, 1% low | `VideoOut` completed guest flips; flip-interval ring buffer | Guest frames only |
| Displayed FPS | `WindowContext::loop.presented_frames` | Kept separate; generated frames never inflate game FPS |
| Frame time | Mean flip interval over the window | ms, tabular figures |
| Emulator CPU | `GetProcessTimes` delta / wall time / logical processors | Labelled "emulator", not system |
| VRAM | `vramStats` used / budget | Hidden if unavailable |
| Shaders compiling | Pipeline compile queue depth | "0 queued" when idle |
| Input | Active pad name, gyro source (physical, tilt, off) | From the controller module |
| Clock profile, FSR | Patch selection; the `FSR window:` counters | FSR shows "Off" or its current state |

A pure `FrameStats` class (a fixed-size ring, no allocations per frame) computes
FPS, 1% low and mean frame time. It samples at 2 Hz on the presentation thread
and never touches guest timing, flips or clocks. GPU load percentage is not shown
because there is no vendor-neutral source.

### Window title

The title gains game FPS, displayed FPS and frame time next to the existing
build, title, GPU and CPU fields, for example
`… | game 30.0 fps · shown 30.0 · 33.3 ms`. This part is testable on the
development PC.

### Overlay

- **F9** cycles the performance overlay through off, compact corner counter and
  full panel. It is rebindable through the input mapping (`PerfOverlay` control)
  and remembered in the emulator's settings. Escape, F1, F6, F7 and F11 keep
  their current meanings.
- The full panel is drawn by the existing ImGui `SystemOverlay` at the top-left
  in a `rail`-coloured rounded panel. The compact counter sits top-right. Both
  avoid the touchpad cursor and dialogs.
- **Revamp of the existing overlays** with the same tokens and DM Sans: system
  dialogs, the IME dialog and on-screen keyboard, trophy toasts and the touchpad
  cursor/trail. That means rounded panels, a white primary button, a white focus
  ring for controller navigation, and consistent spacing. Behaviour, input
  handling and timing stay unchanged; this is a visual restyle with focus
  visuals.

## Testing

Runs on the development PC:
- `FocusGrid` unit tests: movement, wrap, page memory, back history.
- `FrameStats` unit tests: FPS, 1% low and mean from synthetic intervals; ring
  wrap; no growth.
- Token contrast test for the text and focus-ring pairs listed above.
- Playtime recording: start/exit accounting and INI round trip with other keys
  unchanged.
- Hero art selection: `pic0`, then `pic1`, then dominant colour, using fixture
  images.
- An offscreen render test (`QT_QPA_PLATFORM=offscreen`) that builds the
  Home/Library/Settings pages with fixture games, renders them to images and
  checks that they produce non-empty images with the expected focus.
- Title-bar text format test.
- Delivery through `tools/update-build.ps1` to `E:\KytyPS5-Build`, with the
  independent verification used before. The bundled font and its license must
  be installed.

Needs the owner's PC (the renderer does not run on UHD 730), as a checklist:
- Overlay panel and compact counter appear and cycle with F9.
- Values match the title bar and are plausible in Astro's fixed-30 profile.
- Restyled dialogs, IME keyboard and trophy toasts look right and stay
  controller-navigable.
- The launcher's controller navigation works with the AX9 in both modes.

## Order and size

The two parts get separate implementation plans:
1. **Part A** (launcher), split into tasks: tokens and font, `FocusGrid`, Home
   page, Library page, Settings shell, controller navigation, playtime, Classic
   switch.
2. **Part B**: `FrameStats` and the title bar first, then the performance
   overlay, then the restyle of existing overlays.

The owner may swap the order. Sub-projects 2-4 (NS Pro CPU cost, gyro sources,
profiles) stay on the roadmap and slot in independently.

## Out of scope

- Game art downloaded from the internet.
- Themes other than this one plus Classic.
- Store or media features.
- GPU utilisation readouts.
- Any change to emulation, timing, presets or the Astro profile.
