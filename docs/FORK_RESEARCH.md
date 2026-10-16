# KytyPS5 fork research, 17 October 2026

Local baseline: upstream `5dec523143930e35a6a5a9839f972774420b7958`.
Target: Astro Bot PPSA21564 01.007.000, Windows, i5-13500 / RTX 5070 Ti.

## Coverage

- Enumerated 965 direct public forks, recursively enumerated their child forks,
  and included the detached Jetsku repository and its public fork network.
- 1,095 repositories identified; 2,743 published branches and 1,734 unique branch heads.
- Three repositories returned "Repository not found": iotserver24/KytyPS5,
  Ansenuter/KytyPS5 and Quantamyt/KytyPS5. Private/deleted repositories and unpublished
  commits cannot be inspected.
- Fetched divergent histories from 377 repositories in isolated bare repositories;
  principal forks were fetched separately. Deduplicated 19,550 commits beyond the
  baseline history, with 1,989 message/body keyword matches.
- All accessible published branch histories were indexed. Detailed source review was
  focused on relevant candidates; this is not a claim to have manually audited every diff.

Raw evidence is under `_Build/research`: `forks.csv`, `branches.csv`, `commits.csv`,
`forks.json`, `fork-branches.json`, `all-commits.json` and the isolated Git repositories.
The CSVs retain branch names, commit hashes, dates, messages, source repositories
and unavailable-repository errors. These are a point-in-time public snapshot.

## Findings relevant to this crash

| Source / branch | Commit | What it addresses | Local decision |
| --- | --- | --- | --- |
| [Jetsku main](https://github.com/Jetsku/KytyPS5/tree/main) | [b74acf1be](https://github.com/Jetsku/KytyPS5/commit/b74acf1be61ef4b7ac6d39a5248db214e533594f) | Astro GI node indices allocated by fragment helper lanes can overlap, making the linked-list compute shader `657ad04626bf9d55` loop forever. | Adapt the live-lane atomic election while retaining EXEC's allocation count and ranks. Exact shader hash matches local GPU checkpoints. |
| [MarekMarecgy main](https://github.com/MarekMarecgy/KytyPS5/tree/main) | [6740c98ee](https://github.com/MarekMarecgy/KytyPS5/commit/6740c98eec5a2b6740a5985f4c43b3e519cc57b8) | A smaller, newer live-pixel append/consume port for the same Astro shader. | Confirms the focused implementation and source of the failure. |
| [Jetsku main](https://github.com/Jetsku/KytyPS5/tree/main) | [afb409697](https://github.com/Jetsku/KytyPS5/commit/afb409697) | Fragment helpers can spin in emulated compare-exchange retry loops because their atomic results are undefined. | Adapt the helper exclusion around retry loops. The first election-only test passed the compute hang and later failed in a fragment draw. |
| [Jetsku main](https://github.com/Jetsku/KytyPS5/tree/main) | [695fdbe2](https://github.com/Jetsku/KytyPS5/commit/695fdbe2d6a994ec32759313572af8e394961206) | Intel still needs SSE4a instruction compatibility despite the misleading AMD option name. | Keep `--amd-cpu` and `--redzone` in the direct launch shortcut on this Intel rig. |
| [Jetsku main](https://github.com/Jetsku/KytyPS5/tree/main) | [c42b55f9](https://github.com/Jetsku/KytyPS5/commit/c42b55f94b364582e6840c3cc75f4269cde5820b) | INSERTQ upper-half behavior and Windows CPU emulation/red-zone regression coverage. | Candidate CPU correctness work; not a measured FPS fix. |
| [Jetsku main](https://github.com/Jetsku/KytyPS5/tree/main) | [fb4819f0](https://github.com/Jetsku/KytyPS5/commit/fb4819f0c634) | Missing upper clear word caused red ribbons on Astro's galaxy map. | Current upstream color-render-target code already combines both clear words; do not duplicate blindly. |
| [Jetsku main](https://github.com/Jetsku/KytyPS5/tree/main) | [63093019](https://github.com/Jetsku/KytyPS5/commit/63093019a041b8d925156cb2da23b408ea57a8f1), [8aebdc05](https://github.com/Jetsku/KytyPS5/commit/8aebdc058775f8d3a0559abbcabb4e2b67d6fc45) | Preserve newer CPU writes during image readback and retain untouched GPU image bytes during partial writes. | Relevant visual-corruption candidates; substantial cache differences require separate reproduction and tests. |
| [Jetsku main](https://github.com/Jetsku/KytyPS5/tree/main) | [35c2cf0b](https://github.com/Jetsku/KytyPS5/commit/35c2cf0b7) | Race in that fork's deferred page re-protection optimization caused NVIDIA device loss. | Do not import deferred protection as a quick CPU fix. It is not enabled in this checkout. |

## Performance and preload research

| Repository / branch | Relevant evidence | Assessment |
| --- | --- | --- |
| [BryanKAdams/BryKytyPS5, main and perf/astro-amd-20260924](https://github.com/BryanKAdams/BryKytyPS5/branches) | `b63206f4c`: batched RELEASE_MEM submissions and GPU busy statistics; `a9e1ae5` has user reports of successful Astro gameplay. | Useful comparison, but Intel performance reports still identify write-fault storms. Fork FPS results do not predict this PC's performance. |
| [Jetsku/KytyPS5 main](https://github.com/Jetsku/KytyPS5) | Full-lighting path, shader/pipeline replay, SRT trace replay, CPU draw preparation and fault tracking. Latest published int19.1 notes also acknowledge rare NVIDIA device loss. | Best direct source of this crash's helper-lane fixes. Broad performance architecture is not a small cherry-pick. |
| [chenxiao07 experiment/perf-40fps-20260926](https://github.com/chenxiao07/KytyPS5/tree/experiment/perf-40fps-20260926) | `184e1e177`, `90fe38125`: reuse shader compiler storage; `c45f7a3cd`: optional lane-local uniform-load paths; recorded shader hints and memory ownership changes. | Primarily Demon's Souls evidence. Reuse only after checking Astro semantics and memory ownership. |
| [Senaxx main / wolverine](https://github.com/Senaxx/KytyPS5/branches) | `3e25eb099`: wave64 LDS synchronization on subgroup32 hosts; shader and texture fixes, many already upstream. | Compare patch content, not just commit hashes, because upstream and forks have rebased/cherry-picked copies. |
| [EmK530 perf/renderer-optimizations and perf/spirv-optimizations](https://github.com/EmK530/KytyPS5/branches) | `34b190fc9`: submission batching / JIT SRT; `097e7c26f`: draw flushing and dirty-check caching. | Potential CPU work after reliable gameplay is established. |
| [ClGratton/KytyPS5-gratton](https://github.com/ClGratton/KytyPS5-gratton/branches) | `e53bc3edc91e`: exclude helper invocations from fragment ballots; numerous shader, readback and draw-cache branches. | Alternative helper/EXEC semantics need care around WQM and derivatives; do not combine overlapping fixes indiscriminately. |
| [awlewis0927/KytyPS5](https://github.com/awlewis0927/KytyPS5) | `2aeb4a9eb208`, `46935d00d5ed`, `6e43944efd7f`: sticky-page write-fault experiments. | Experimental CPU optimization; correctness and same-scene measurements required. |

No wholesale fork merge or universal FPS claim follows from this research. Actual
local GPU checkpoint logs and repeat game launches decide whether an adapted fix works.
