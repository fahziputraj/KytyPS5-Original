# Local build delivery

The user wants the latest runnable Windows build at `F:\KytyPS5-Build`.
Replace the binaries in that directory on each update. Preserve existing settings,
shader/pipeline caches, and save data. Use `tools/update-build.ps1` to build, test,
and stage source-build updates in that location.

The user subsequently selected Jetsku U59 int19.1 as the active runnable distribution:
https://github.com/Jetsku/KytyPS5/releases/tag/u59-windows-20261013-int19.1
Preserve that release deployment unless the user requests another distribution or
a source-built replacement. See docs/JETSKU_RELEASE_INSTALL.md and BUILD-INFO.json
in the installed directory for the active binaries, preset and backup.
