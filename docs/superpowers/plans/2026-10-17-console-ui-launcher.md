# Console-Style Launcher (Part A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the launcher's default layout with the approved console-style UI: a hero-row
Home, a Library hub, a Settings shell with per-game origin, launcher controller navigation and
playtime. The current layout stays available as "Classic".

**Architecture:** New Qt Widgets pages, custom-painted where the look needs it, in a
`QStackedWidget` inside `MainDialog`. Pages consume a plain `GameCard` model built from the
existing `ConfigurationListWidget` data, so they can be tested offscreen with fixtures. Logic
lives in pure classes with unit tests: focus, search, overrides, playtime and art selection.
Game data, settings logic and launching stay where they are today.

**Tech Stack:** C++20, Qt 6.10.3 Widgets and Concurrent (no Qt Quick), SDL3 via the existing
`ControllerPreview` pump, CMake/CTest, clang-cl 22.1.3.

**Spec:** `docs/superpowers/specs/2026-10-17-console-ui-design.md` (Part A, "Shared visual
system", "Testing").

## Global Constraints

- Work on `feature/global-pacing-fsr`, in this checkout. The owner asked for commits to be pushed to
  `origin` as well: push after each task's commit with `git push origin feature/global-pacing-fsr`
  (never force).
- Every new commit ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Token values, sizes and timings come from the spec's table:
  - colours: `bg #0A1020`, `rail #070C18`, `surface-1 #0F1A33`, `surface-2 #16223D`,
    `hero-base #132A52`, `hero-accent #1D3F78`, `accent #3B82F6`,
    `text-primary #E6ECF5`, `text-secondary #B7C6E3`, `text-muted #7D8DB0`,
    `success #86EFAC`, `warning #FBBF24`, `danger #F87171`;
  - focus ring: white, 2 px, offset 3 px; primary button white with `#0A1020` text;
  - radius 10 px for tiles, 12 px for cards; 4/8 px spacing scale;
  - type: 13/15/18/24/32 px, weights 400 and 500;
  - focus animation 150-200 ms ease-out, page cross-fade 200 ms; "Reduce motion" disables both.
- No Sony logos, wordmarks, fonts or trademarked artwork. Controller hints use the connected
  pad's labels: A/B for Xbox-style pads, face-button names for a DualSense.
- Every icon-only button sets `setAccessibleName`. Every action works by keyboard, mouse and
  controller.
- Refinement of the spec's mockup category names: Settings categories map to the dialog's
  existing whole group boxes, so no setting moves between groups (see Task 7). Search still
  reaches every setting.
- Do not change emulator code, timing, presets, the Astro shortcut, the installer, or existing
  settings keys and their meaning. New keys are `[Launcher] layout` (`console` or `classic`),
  `[Launcher] reduce_motion`, and the `[Playtime]` group only.
- Downloading the DM Sans font needs the owner's approval at execution time (state the file,
  source and size). Never fetch it silently.
- Format changed lines with
  `python -I _Build/deps/windows-toolchain/LLVM/bin/git-clang-format --binary _Build/deps/windows-toolchain/LLVM/bin/clang-format.exe --style file:src/.clang-format`
  (stage the files first).
- **BUILD:** `powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\build-windows-local.ps1' -Targets <comma list> -Jobs 4"`.
- **CTEST:** run `ctest.exe` from the toolchain's CMake folder with `--test-dir _Build\windows-int19 --output-on-failure -R '<regex>'`, with `_Build\windows-int19\launcher` and the Qt `bin` folder on `PATH`.
- **UI TESTS:** target `launcher_console_ui_tests`, CTest name `launcher_console_ui`. It runs
  with `QT_QPA_PLATFORM=offscreen` set through `set_tests_properties(... ENVIRONMENT ...)`.

## Review Focus

1. **Empty library** (no game folder, or zero games). Home shows an invitation with an
   **Add game folder** action instead of a blank hero. Library shows the same. Test in Task 5.
2. **Large library** (500 games). The Home tile row shows the 12 most recently played (then
   alphabetical) plus an **All games** tile. The Library grid scrolls without loading all 500
   images at once. Art loads off the UI thread through a cache bounded to 64 entries. Test in
   Tasks 3 and 6.
3. **Missing, truncated or oversized images, and archive games.** Fall back to the dominant
   colour or the flat backdrop; no crash, no blank tile. Test in Task 3.
4. **A game is running.** Play stays disabled, and launcher controller navigation stops so it
   never competes with the game. Test in Task 8.
5. **Switching Classic and Console layouts** changes only `[Launcher] layout`. Every game and
   global setting key stays byte-identical in `Kyty.ini`. Test in Task 9.

---

### Task 1: Theme tokens, contrast check, bundled font, UI test target

**Files:**
- Create: `src/launcher/include/consoleTheme.h`, `src/launcher/src/consoleTheme.cpp`,
  `src/launcher/styles/console.qss`
- Create: `src/launcher/fonts/DMSans.ttf` (variable font), `LICENSES/DM-Sans-OFL.txt`
- Modify: `src/launcher/resources.qrc` (prefix `/fonts`, `console.qss`),
  `src/launcher/CMakeLists.txt` (new test target)
- Modify: `tools/build-windows-local.ps1` (default targets add `launcher_console_ui_tests`),
  `tools/update-build.ps1` (gate regex adds `launcher_console_ui`)
- Test: `tests/LauncherConsoleUiTests.cpp`

**Interfaces:**
- Produces:
  - `namespace ConsoleTheme { struct Tokens { QColor bg, rail, surface1, surface2, hero_base, hero_accent, accent, text_primary, text_secondary, text_muted, success, warning, danger, focus; }; const Tokens& Get(); double ContrastRatio(const QColor& foreground, const QColor& background); QString FontFamily(); bool LoadFonts(); QString StyleSheet(); }`.
  - `LoadFonts()` registers `:/fonts/DMSans.ttf` and returns false when it can't. `FontFamily()`
    then returns `"Segoe UI"`.
  - `StyleSheet()` returns `console.qss` with `@token@` placeholders replaced by the token hex
    values.

- [ ] **Step 1: Ask the owner to approve the font download**

Ask for approval to fetch `ofl/dmsans/DMSans[opsz,wght].ttf` and `ofl/dmsans/OFL.txt` from
`github.com/google/fonts`. Give the pinned commit and each file's size from the API listing.
After approval, download into a new empty folder under `_Build/downloads/dmsans`, record both
SHA-256 values in the commit message, and copy them into place (`DMSans.ttf`,
`DM-Sans-OFL.txt`).

- [ ] **Step 2: Write failing tests**

In `tests/LauncherConsoleUiTests.cpp`, a `main()` with `QApplication` and a `check` lambda in
the style of `LauncherPresentationConfigTests`:
- `TestTokenContrast`:
  - `ContrastRatio(text_primary, X) >= 4.5` and `ContrastRatio(text_secondary, X) >= 4.5` for
    X in {bg, rail, surface1, surface2, hero_base, hero_accent};
  - `ContrastRatio(text_muted, X) >= 4.5` for X in {bg, rail, surface1};
  - `ContrastRatio(focus, X) >= 3.0` for every surface;
  - `ContrastRatio(bg, white) >= 4.5` (primary button text);
  - `ContrastRatio(Qt::black, Qt::white)` is about 21.
- `TestFontsAndStyleSheet`: `LoadFonts()` returns true and the family name is `"DM Sans"`;
  `StyleSheet()` contains `#0A1020` and no `@`.

- [ ] **Step 3: Add the CMake target and watch it fail**

The target is built from `tests/LauncherConsoleUiTests.cpp` plus `src/consoleTheme.cpp`, links
`Qt6::Widgets Qt6::Concurrent`, and includes `include` and `${CMAKE_SOURCE_DIR}/src`. Use
`add_test(NAME launcher_console_ui ...)` with the `ENVIRONMENT QT_QPA_PLATFORM=offscreen`
property.

Run: BUILD `launcher_console_ui_tests`. Expected: FAIL (compile errors for the missing
`ConsoleTheme`).

- [ ] **Step 4: Implement `ConsoleTheme`, `console.qss` and the resources**

Implement `ContrastRatio` with the WCAG relative-luminance formula: sRGB linearised with the
0.03928 threshold, ratio = (L1+0.05)/(L2+0.05). `console.qss` styles the shared widgets with
the tokens: page background, `QLineEdit#search`, pill buttons, the primary white button,
rail buttons and the focus ring through `:focus` outlines.

- [ ] **Step 5: Run `launcher_console_ui`; expect PASS. Wire the gate scripts; commit and push**

Commit message: `feat(launcher): console theme tokens and bundled DM Sans`.

### Task 2: Focus model and page history

**Files:**
- Create: `src/launcher/include/focusGrid.h`, `src/launcher/src/focusGrid.cpp`
- Test: `tests/LauncherConsoleUiTests.cpp`; the target gains `src/focusGrid.cpp`

**Interfaces:**
- Produces:
  - `class FocusGrid { public: enum class Direction { Left, Right, Up, Down }; void SetItems(int count, int columns); int Current() const; void SetCurrent(int index); bool Move(Direction direction); };`
    - Left/Right step one item and cross row ends, but stop at the first and last item.
    - Up/Down step by `columns`; Down clamps to the last item of a partial last row.
    - `Move` returns false when focus would leave the grid, so the caller can move to another
      region.
    - `SetItems` keeps `Current` if it is still valid, otherwise clamps it; `-1` means empty.
  - `class PageHistory { public: struct Entry { QString page; int focus; }; void Visit(const QString& page, int focus); std::optional<Entry> Back(); int FocusFor(const QString& page) const; };`
    `FocusFor` returns `0` for an unseen page.

- [ ] **Step 1: Write failing tests**

- `TestFocusGrid` cases:
  - 7 items in 3 columns: Right from 2 goes to 3; Down from 4 goes to 6; Down from 5 clamps to
    6; Up from 1 returns false.
  - A single row of 5 (columns 5): Left from 0 returns false.
  - `SetItems(0, 3)` makes `Current() == -1`, and every `Move` returns false.
- `TestPageHistory`: visit home(2), then library(5), then settings(0). `Back()` returns
  library/5, then home/2. `FocusFor("library") == 5`.

- [ ] **Step 2: Run; expect FAIL** (undefined `FocusGrid`).
- [ ] **Step 3: Implement the two classes**
- [ ] **Step 4: Run `launcher_console_ui`; expect PASS. Commit and push** (`feat(launcher): focus grid and page history`).

### Task 3: Game cards, art selection and the art cache

**Files:**
- Create: `src/launcher/include/gameCard.h`, `src/launcher/include/gameArt.h`,
  `src/launcher/src/gameArt.cpp`
- Test: `tests/LauncherConsoleUiTests.cpp`; the target gains `src/gameArt.cpp`,
  `src/gameContent.cpp` and the sources `gameContent.cpp` needs

**Interfaces:**
- Produces:
  - `struct GameCard { QString game_path, title, title_id, version, basedir, elf, clock_profile, controller; Configuration::GameStatus status = Configuration::GameStatus::Unknown; std::optional<qint64> last_played_ms; qint64 playtime_s = 0; std::optional<qint64> shader_cache_bytes; };`
  - `namespace GameArt { QString BackdropFile(const QString& basedir); QImage LoadImage(const QString& basedir, const QString& relative); QColor DominantColor(const QImage& image); QColor BackdropColor(const QImage& icon); std::optional<qint64> ShaderCacheBytes(const QString& runtime_dir, const QString& title_id); }`
    - `BackdropFile` returns `sce_sys/pic0.png`, else `sce_sys/pic1.png`, else empty, using
      `GameContent::FileExists`.
    - `LoadImage` reads through `GameContent::ReadFile` with `GameContent::MaxImageSize` and
      returns a null image on failure.
    - `BackdropColor` darkens the dominant colour until
      `ContrastRatio(text_primary, colour) >= 4.5`, and returns `hero_base` for a null icon.
    - `ShaderCacheBytes` sums the sizes of `_PipelineCache/<TITLE_ID>.*`, or returns empty when
      there are none. It never writes.
  - `class ArtCache : public QObject { Q_OBJECT public: explicit ArtCache(int capacity = 64, QObject* parent = nullptr); void Request(const QString& key, const QString& basedir, const QString& relative, QSize size); std::optional<QPixmap> Get(const QString& key) const; int Size() const; signals: void Ready(const QString& key); };`
    - It decodes and scales with `QtConcurrent::run` and evicts the least recently used entry
      past `capacity`.
    - `key` = `game_path + "|" + relative + "|" + size`.

- [ ] **Step 1: Write failing tests** (fixture game folders under a `QTemporaryDir`, written with `QImage::save`)

- `TestBackdropSelection`: both pic0 and pic1 give pic0; only pic1 gives pic1; neither gives
  empty.
- `TestArtFallbacks`:
  - a truncated PNG, and a file larger than `MaxImageSize`, both give a null image;
  - `BackdropColor(QImage())` returns `hero_base`;
  - a pure yellow icon gives a colour where `ContrastRatio(text_primary, colour) >= 4.5`.
- `TestShaderCacheBytes`: two files of 10 and 20 bytes for `PPSA00001` give 30; another title
  gives empty; the folder's modification time is unchanged.
- `TestArtCacheBound` (Review Focus 2): request 100 keys at capacity 64, wait for all `Ready`
  signals with a `QEventLoop` and a 10 s timeout; then `Size() == 64` and the oldest key is gone.

- [ ] **Step 2: Run; expect FAIL** (missing headers).
- [ ] **Step 3: Implement `gameCard.h`, `GameArt`, `ArtCache`**
- [ ] **Step 4: Run `launcher_console_ui`; expect PASS. Commit and push** (`feat(launcher): game cards, art fallbacks and a bounded art cache`).

### Task 4: Playtime store

**Files:**
- Create: `src/launcher/include/playtime.h`, `src/launcher/src/playtime.cpp`
- Test: `tests/LauncherConsoleUiTests.cpp`

**Interfaces:**
- Produces: `class PlaytimeStore { public: explicit PlaytimeStore(QSettings* settings); void Start(const QString& title_id, qint64 now_ms); void Stop(const QString& title_id, qint64 now_ms); void DropStaleSessions(); struct Record { std::optional<qint64> last_played_ms; qint64 seconds = 0; }; Record Get(const QString& title_id) const; };`
  - Keys under group `Playtime`: `<TITLE_ID>/last_played`, `<TITLE_ID>/seconds`,
    `<TITLE_ID>/running_since`.
  - `Start` sets `last_played` and `running_since`.
  - `Stop` adds `(now - running_since)/1000` to `seconds`, ignoring negative values, and removes
    `running_since`.
  - `DropStaleSessions` removes every `running_since` without adding time; it runs at launcher
    start, after an unobserved exit.

- [ ] **Step 1: Write failing tests**

- `TestPlaytime`: start at 1,000,000 and stop at 1,090,000 gives 90 s and last played
  1,000,000. A second session of 30 s gives 120. A stop without a start changes nothing.
  `DropStaleSessions` after a start adds nothing.
- `TestPlaytimeLeavesOtherKeys`: write `[GlobalConfiguration] fsr_upscale=off` and
  `[Launcher] grid_view=true`, run a session, then confirm both are unchanged in a re-read.

- [ ] **Step 2: Run; expect FAIL.** **Step 3: Implement.** **Step 4: Run; expect PASS. Commit and push** (`feat(launcher): record playtime per title`).

### Task 5: Home page (direction A)

**Files:**
- Create: `src/launcher/include/homePage.h`, `src/launcher/src/homePage.cpp`
- Test: `tests/LauncherConsoleUiTests.cpp`

**Interfaces:**
- Consumes: `ConsoleTheme`, `FocusGrid`, `GameCard`, `ArtCache` (Tasks 1-3).
- Produces: `class HomePage : public QWidget { Q_OBJECT public: explicit HomePage(ArtCache* art, QWidget* parent = nullptr); void SetGames(const QList<GameCard>& games); void SetRunEnabled(bool enabled); void SetReduceMotion(bool reduce); int CurrentIndex() const; signals: void PlayRequested(int index); void SettingsRequested(int index); void TrophiesRequested(int index); void LibraryRequested(); void AddFolderRequested(); };`
  - **Tile row:** up to 12 games, ordered by `last_played_ms` descending then title, plus an
    **All games** tile that emits `LibraryRequested`.
  - **Visible children** with object names `title_label`, `chip_title`, `chip_clock`,
    `chip_controller`, `play_button`, `settings_button`, `trophies_button`, `card_last_played`,
    `card_compatibility`, `card_shader_cache`, and `empty_state` with `add_folder_button`.
  - **Chips and cards** without data are hidden.
  - **Keys:** Left/Right move between tiles. Down moves to the buttons and Up returns to the
    tiles. Return activates. Escape goes back to the tiles.
  - **Backdrop** is painted from `BackdropFile`, else `BackdropColor(icon)`.
  - **Focus** is painted as a scaled tile (1.08) with a white 2 px ring at a 3 px offset,
    animated by `QVariantAnimation` unless reduce-motion is on.

- [ ] **Step 1: Write failing tests**

- `TestHomePageNavigation`, with 3 fixture cards and the page shown offscreen:
  - Right then Return emits `PlayRequested(1)`.
  - Down, Right, Return emits `SettingsRequested(1)`.
  - Rendering with `grab()` gives a non-null 1280×720 image whose centre pixel is not pure
    black.
- `TestHomePageEmpty` (Review Focus 1): `SetGames({})` shows `empty_state`, hides
  `play_button`, and clicking `add_folder_button` emits `AddFolderRequested`.
- `TestHomePageChips`: a card with an empty `clock_profile` and `controller` hides
  `chip_clock` and `chip_controller`. A card without `last_played_ms` hides
  `card_last_played`.
- `TestHomePageLimit`: 30 cards give 13 tiles, with the last one being **All games**.
- `TestHomePageRunDisabled`: after `SetRunEnabled(false)`, Return on a tile emits nothing.

- [ ] **Step 2: Run; expect FAIL.** **Step 3: Implement.** **Step 4: Run; expect PASS. Commit and push** (`feat(launcher): console home page`).

### Task 6: Library page (direction B)

**Files:**
- Create: `src/launcher/include/libraryPage.h`, `src/launcher/src/libraryPage.cpp`
- Test: `tests/LauncherConsoleUiTests.cpp`

**Interfaces:**
- Produces:
  - `bool MatchesSearch(const GameCard& card, const QString& text);` is case-insensitive over
    title, title ID and version; empty text matches.
  - `class LibraryPage : public QWidget { Q_OBJECT public: explicit LibraryPage(ArtCache* art, QWidget* parent = nullptr); void SetGames(const QList<GameCard>& games); void SetRunEnabled(bool enabled); int VisibleCount() const; signals: void PlayRequested(int index); void SettingsRequested(int index); void NavigateRequested(const QString& page); };`
    - **Rail** buttons `rail_home`, `rail_library`, `rail_controllers`, `rail_trophies`,
      `rail_settings` emit `NavigateRequested("home"|...)`.
    - **Top:** `search` `QLineEdit` and `continue_card` for the most recently played game.
    - **Grid:** a `QListView` (`cover_grid`) in icon mode with uniform item sizes, using a
      `CoverDelegate` that paints the cover from `ArtCache` (placeholder until `Ready`), the
      title and the focus ring.
    - **Model:** `QSortFilterProxyModel` with `MatchesSearch`.
    - **Keys:** Left/Right/Up/Down move in the grid through `FocusGrid`. Return emits
      `PlayRequested`. The Menu key or Start emits `SettingsRequested`. Escape emits
      `NavigateRequested("home")`.

- [ ] **Step 1: Write failing tests**

- `TestMatchesSearch`: "astro" matches the title "Astro Bot"; "ppsa21564" matches; "01.007"
  matches; "zzz" does not.
- `TestLibraryFilterAndKeys`: 5 cards; typing "astro" gives `VisibleCount() == 1`; clearing
  gives 5; Return on the first emits `PlayRequested` with the index of the source card.
- `TestLibraryLarge` (Review Focus 2): with 500 cards, `SetGames` returns in under 500 ms, and
  the `ArtCache` request count after showing the page is at most the number of visible cells
  plus one row.
- `TestLibraryEmpty`: no cards shows the same invitation with `add_folder_button`.

- [ ] **Step 2: Run; expect FAIL.** **Step 3: Implement.** **Step 4: Run; expect PASS. Commit and push** (`feat(launcher): library hub with search and cover grid`).

### Task 7: Settings shell with origin badges and search

**Files:**
- Create: `src/launcher/include/settingsPage.h`, `src/launcher/src/settingsPage.cpp`,
  `src/launcher/include/settingsOrigin.h`, `src/launcher/src/settingsOrigin.cpp`
- Modify: `src/launcher/src/configurationEditDialog.cpp`, adding a `SetEmbedded(bool)` that
  removes the dialog frame and button box when hosted. Its data and save logic are unchanged.
- Test: `tests/LauncherConsoleUiTests.cpp`

**Interfaces:**
- Produces:
  - `QStringList OverriddenSettings(const Configuration& game, const Configuration& global);`
    returns the keys of `GameSettings()` whose values differ, sorted.
  - `bool ResetToGlobal(Configuration& game, const Configuration& global, const QString& key, QString& error);`
    uses `SetGameSettings` with a one-key `QJsonObject` holding the global value.
  - `struct SettingsCategory { QString id, title; QStringList group_object_names; };` and
    `QList<SettingsCategory> SettingsCategories();` map each rail entry to whole group boxes:
    - `display` "Display": `graphics_group` (also holds presentation and CPU scheduling);
    - `pacing` "Pacing and upscaling": `pacing_group`;
    - `controller` "Controller": `controller_group`;
    - `audio` "Audio": `audio_group`;
    - `compatibility` "Compatibility": `compatibility_group` (AMD CPU instructions);
    - `profile` "Profile and notifications": `user_profile_group`, `notifications_group`;
    - `advanced` "Advanced": `debug_group`, plus the launcher's Classic and Reduce motion
      options from Task 9.
  - `QStringList SearchCategories(const QWidget* root, const QString& text);` returns category
    IDs whose groups contain a `QLabel`, `QCheckBox` or `QAbstractButton` text matching `text`.
  - `class SettingsPage : public QWidget { Q_OBJECT public: SettingsPage(QWidget* parent = nullptr); void Edit(ConfigurationEditDialog* embedded, const Configuration* game, const Configuration& global); signals: void Closed(bool saved); };`
    - **Rail** `rail_<id>`, a `search` field and a `override_badge` reading
      "Per-game · N changed", hidden for global settings.
    - **Row badges:** each overridden row gets an "Inherited" or "Set for this game" label
      found by setting key. A **Reset to global** button calls `ResetToGlobal`.
    - **Hint:** the Controller category shows the dialog's existing motion-detection status
      (`label_controller_motion_status`, filled by `DetectMotionControllers`) at the top. There
      is no new detection, and a hint never claims an undetected capability.

- [ ] **Step 1: Write failing tests**

- `TestOverriddenSettings`: a global with defaults and a game with `fsr_upscale="quality"` and
  `presentation_fps=45` gives exactly those two keys.
- `TestResetToGlobal`: resetting `fsr_upscale` gives `"off"` and leaves one key.
- `TestSearchCategories`: a fixture widget tree with a `graphics_group` containing a "Screen
  resolution" label matches "resolution" to `display`.
- `TestSettingsPageBadge`: a game with two overrides shows the badge text
  "Per-game · 2 changed". Global settings hide it.

- [ ] **Step 2: Run; expect FAIL.** **Step 3: Implement.** **Step 4: Run; expect PASS. Commit and push** (`feat(launcher): settings shell with origin and search`).

### Task 8: Controller navigation in the launcher

**Files:**
- Modify: `src/launcher/include/controllerPreview.h`, `src/launcher/src/controllerPreview.cpp`
- Create: `src/launcher/include/gamepadNavigation.h`, `src/launcher/src/gamepadNavigation.cpp`
- Test: `tests/LauncherConsoleUiTests.cpp`

**Interfaces:**
- Produces:
  - `int KeyForGamepadButton(int sdl_button);` (in `gamepadNavigation.h`) maps:
    - D-pad → `Qt::Key_Left/Right/Up/Down`;
    - South → `Qt::Key_Return`;
    - East → `Qt::Key_Escape`;
    - Start → `Qt::Key_Menu`;
    - left/right shoulder → `Qt::Key_PageUp/PageDown`;
    - anything else → 0.
  - `int KeyForLeftStick(int x, int y, int previous_key);` uses a deadzone of 16000 with
    hysteresis (release below 8000) and returns one key per push.
  - `bool ShouldPump(bool suspended, bool preview_enabled, bool color_valid, bool navigation_enabled);`
    returns `!suspended && (preview_enabled || color_valid || navigation_enabled)`.
  - `ControllerPreview::SetNavigationEnabled(bool)` and `signal NavigationKey(int qt_key)`.
    Button-down and stick events from the existing single `SDL_PollEvent` loop in `Refresh()`
    are translated with the functions above. `UpdateRunning()` uses `ShouldPump`.

- [ ] **Step 1: Write failing tests**

- `TestGamepadKeys`: each mapping above, and an unknown button gives 0.
- `TestStickHysteresis`: x=20000 gives Right; x=12000 while held gives 0; x=4000 releases;
  20000 again gives Right.
- `TestNavigationGating` (Review Focus 4): `ShouldPump(true, …, true)` is false; and
  `ShouldPump(false, false, false, true)` is true.

- [ ] **Step 2: Run; expect FAIL.** **Step 3: Implement.** **Step 4: Run; expect PASS. Commit and push** (`feat(launcher): controller navigation through the shared SDL pump`).

### Task 9: MainDialog integration, Classic switch and playtime wiring

**Files:**
- Modify: `src/launcher/src/mainDialog.cpp`, `src/launcher/include/mainDialog.h`,
  `src/launcher/forms/main_dialog.ui`, `src/launcher/src/launcherTheme.cpp`,
  `src/launcher/include/configurationListWidget.h`, `src/launcher/src/configurationListWidget.cpp`
- Test: `tests/LauncherConsoleUiTests.cpp`

**Interfaces:**
- Consumes: Tasks 1-8.
- Produces:
  - `QList<GameCard> ConfigurationListWidget::GameCards(const PlaytimeStore& playtime) const;`
    builds cards from the existing items:
    - status from the item's `Configuration::game_status`, the value `ApplyCompatibility`
      already fills from `CompatibilityDatabase`;
    - clock profile from the per-game cheats document, as the settings dialog reads it;
    - controller from the last detection;
    - shader cache through `GameArt::ShaderCacheBytes(m_runtime_directory, …)`.
  - `bool ConfigurationListWidget::SelectGame(const QString& game_path);` selects the item so
    the existing `Run`, edit, trophies and mapping slots apply to it.
  - `enum class LauncherLayout { Console, Classic }; LauncherLayout ReadLayout(QSettings&); void WriteLayout(QSettings&, LauncherLayout);`
    reads and writes `[Launcher] layout`, defaulting to Console.
- `MainDialog` hosts a `QStackedWidget`:
  - pages Home, Library and Settings;
  - Controllers, which re-hosts the existing `InputMappingDialog` the same way as Settings;
  - Trophies, which reuses `ViewTrophyOverview`.
- `PageHistory` drives Back. Console layout applies `ConsoleTheme::StyleSheet()`; Classic
  keeps today's widget and `launcher.qss`.
- Play on any page calls `SelectGame`, then the existing `MainDialogPrivate::Run`.
- `PlaytimeStore::Start` runs in `Run`, `Stop` on `QProcess::NotRunning`, and
  `DropStaleSessions` at startup.
- `ControllerPreview::SetNavigationEnabled(window active && process not running)` updates on
  `QGuiApplication::applicationStateChanged` and process state changes.
- A **Classic layout** check box sits under Settings > Advanced (launcher section), next to
  **Reduce motion** (`[Launcher] reduce_motion`).

- [ ] **Step 1: Write failing tests**

- `TestLayoutSetting` (Review Focus 5): write a fixture `Kyty.ini` with global and per-game
  settings. `WriteLayout(Classic)` then `WriteLayout(Console)` leaves every key outside
  `[Launcher]` byte-identical, compared by hash over the re-serialised groups.
- `TestGameCardsFromList`: a `ConfigurationListWidget` pointed at a `QTemporaryDir` holding two
  fixture games with `param.json` gives 2 cards with the correct title IDs and versions.
- `TestNavigationDisabledWhileRunning`: a stubbed process-state signal sequence toggles
  `SetNavigationEnabled` false, then true on exit.

- [ ] **Step 2: Run; expect FAIL.** **Step 3: Implement.**
- [ ] **Step 4: Build `launcher` and run UI TESTS plus `launcher_presentation_config`; expect PASS. Commit and push** (`feat(launcher): console layout with Classic fallback`).

### Task 10: Docs, delivery and verification

**Files:** Create `docs/LAUNCHER-CONSOLE-UI.md`. Modify `docs/WINDOWS-LOCAL-BUILD.md` (font
license and new test).

- [ ] **Step 1: Document**

Cover the pages, keys and controller buttons, the Classic switch, Reduce motion, playtime, what
is read and never written (pipeline-cache sizes), the font license, and a home-PC checklist:
- the AX9 navigates Home, Library and Settings in XInput and NS Pro modes;
- navigation stops while a game runs;
- art appears for the owner's games;
- the Classic switch restores today's layout.

Commit and push (`docs(launcher): console UI guide`).

- [ ] **Step 2: Run the developer suite and the PowerShell 7 fixture checks**

Run `tools/test-temporal.ps1` with both SDK roots (expect 8/8), then `pwsh` runs of
`tools/test-runtime-install.ps1` (71) and `tools/test-astro-launcher.ps1` (30).

- [ ] **Step 3: Deliver from a clean tree**

Snapshot `E:\KytyPS5-Build` hashes, then run
`./tools/update-build.ps1 -Destination E:\KytyPS5-Build -Fsr3Probe -SkipFsr3GpuTest -DlssPrototype -Jobs 3`.
If the launcher is open from E:, ask the owner to close it.

Expected: exit 0, and the gate log shows `launcher_console_ui` passing.

- [ ] **Step 4: Verify independently**

Check:
- the manifest commit equals HEAD with `modified: false`;
- staged files match;
- pre-existing user files are byte-identical;
- `LICENSES\DM-Sans-OFL.txt` is installed;
- the launcher stays up for 8 s with the separate test INI;
- that INI gains only `[Launcher] layout` (and nothing else) on first start, then stays
  unchanged on a second start.
