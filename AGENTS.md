# Local build delivery

The user wants the latest runnable Windows build at `F:\KytyPS5-Build`.
Replace the binaries in that directory on each update. Preserve existing settings,
shader/pipeline caches, and save data. Use `tools/update-build.ps1` to build, test,
and stage source-build updates in that location.

Use standard branch prefixes such as `feature/`, `fix/`, `chore/`, and `test/`.
The user explicitly rejected the `codex/` prefix for new branches. Historical
backup branches with that prefix may remain unchanged.

The user selected Jetsku U59 int19.1 as the source base and subsequently requested
rebasing local work onto it and fixing CPU load / slow motion with variable FPS:
https://github.com/Jetsku/KytyPS5/releases/tag/u59-windows-20261013-int19.1
Use that exact release tag as the base. The previous official-base experiments
are preserved on `codex/backup-pre-jetsku-20261017`. Do not replay the old shader
cache replacement over Jetsku's cache. See `docs/ASTRO-TIMING.md` for the current
timing changes, local PGO build/junction handling, validation and delivery.
Keep the guest vblank frequency at 60 for Astro; lowering it to 30 is not a
gameplay-speed fix. A presentation cap and hardware VRR are separate facilities.
The experimental Astro 01.007 patch pairs a guarded 30 FPS engine step with a
30 FPS presentation cap. Preserve its version/byte checks and keep it opt-out.
The user rejected the accurate graphics profile and the subsequent uncapped test
as regressions (slow loading, missing visuals). Their latest request is to restore
the earlier 30 FPS profile plus P-core optimization. Use Performance1080p with the
guarded 30 FPS clock patch, KYTY_CPU_HYBRID=on and CP reservation. Keep E-cores for
background workers. Preserve their saves and keep adaptive clock experiments on
codex/astro-adaptive-clock-experiment. The latest request is to try adaptive FPS
with maintained game pace. Keep the fixed 30 FPS entry point as the default and
offer the bounded 30-60 FPS experiment as a separate launch option. Validate it
using a save copy; do not claim full-speed gameplay below its 30 FPS floor or
correct gameplay pace from the FPS counter alone.

The latest request extends the reusable controls to other games and investigates
FSR frame generation. Work on `feature/global-pacing-fsr` and commit there
first; the intended proposal target is Jetsku's fork. Preserve the working Astro
profile. Global presentation/CPU controls do not authorize guessing game clock
addresses or FSR guide textures. See `docs/GLOBAL-PACING-FSR-PROPOSAL.md` for the
typed clock schema, optional SDK GPU prototype, validation and remaining game
integration prerequisites.
