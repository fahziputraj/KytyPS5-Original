# Wolverine startup investigation checkpoint

Paused on 2026-10-17 at the user's request to resume Astro/FSR work.
**No Wolverine startup fix has been established or installed.**

## Reproduction

PPSA03671, content version 01.001.005, crashes on the installed cb993525 build
before gameplay. Performance settings, completion labels with idle suspend points,
and a clean compatibility environment all reached the same null read at eboot
RVA `0403fb16`. No Astro clock patch was loaded.

The failing instruction dereferences an object selected from an engine table.
The frame-pointer chain recovered by the debugger includes guest return RVAs
`040450c2` and `03d76481`. The ordinary host unwind does not recover this chain.

## Evidence collected

- The table's active mask has three slots set, but its captured pointer entries
  are zero at the failure.
- Three valid objects with non-null vtables are passed to registration at RVA
  `040411f0`, from calls at `04012737`, `04012758`, and `040127c0`.
- A CPU data breakpoint confirms the first registration store at `0404127e`
  writes the expected non-null object pointer.
- No later write to that slot was observed on the initializing thread before
  the crash. Writes on another thread, through another mapping, or by the GPU
  are not excluded. The cause of the clearing remains unknown.
- The unresolved import `juWbTNM+8hw` is POSIX `fsync`. Its current stub returns
  success. That missing implementation does not establish the cause of the
  object-table failure.

The extracted directory contains 301 non-empty files totaling about 183.17 GiB.
This inventory is not an integrity check against the original package.
The tested process also loaded third-party overlays; these tests do not exclude
an overlay interaction.

## Resume work

Trace writes across all live and newly created threads, handling threads that
exit before their context can be queried. If no virtual-address write explains
the clearing, inspect backing aliases and mapping lifetime before assuming a GPU
overwrite. Compare package metadata and a known compatible emulator build as
independent controls. Do not skip the null dereference or fabricate objects.

Private debugger captures, helpers, logs and game disassembly remain in ignored
`_Build` files. No game binary or proprietary shader/code dump is included in
this checkpoint. The user's installed working build was left unchanged.
