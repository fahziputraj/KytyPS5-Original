# Active installation: Jetsku int19.1

The runnable installation in `F:\KytyPS5-Build` uses the unmodified binaries from
[Jetsku U59 int19.1](https://github.com/Jetsku/KytyPS5/releases/tag/u59-windows-20261013-int19.1).
It replaces the earlier local experimental build in that folder.

Start Astro Bot with `Start-AstroBot.cmd`, or use `launcher.exe` for the game library.
`kyty_launcher.exe` is an identical copy of the release launcher for old shortcuts.
The game is at `H:\PS5 Games\PPSA21564-app`.

The release ZIP SHA-256 was verified against GitHub's asset digest:
`9ec5f56ee62a21f86004fd789200a83820431fd05fa97093e7d839011a70e2ff`.
`BUILD-INFO.json` records the installed executable hashes, release URL and backup path.

The bundled `u59-preset.json` is installed beside the launcher. Its
`KYTY_DEFER_PROTECTS` value is `0`, the release's documented NVIDIA device-loss
workaround. Shader precompilation, learned pipeline replay and the program cache
stay enabled. The first visit to an area still has to learn its shaders.

The direct shortcut loads the same preset into its own process environment,
clears inherited KYTY/TRACY overrides, and launches at a 1920x1080 window size,
FIFO presentation and 60 Hz. It enables Intel instruction compatibility,
red-zone protection and tessellation, and keeps accurate occlusion off.
No Windows power, security or machine-wide environment settings are changed.

The local `Kyty.ini` retains the user's controls, identity, display and audio
preferences. CPU compatibility, red-zone protection and tessellation are enabled;
debug validation/profiling stay off. The library searches `H:\PS5 Games`.

Saves and caches were retained in place. The two existing save files were
hash-checked unchanged during installation and copied into the backup alongside
the old executables and settings. Normal game saves after launch can update them.

Backups are under `F:\KytyPS5-Build-Backups`. Do not clear the cache directories
between launches. Use the installed `launcher.exe` for other games; compatibility
must be checked per title.
