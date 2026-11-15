# Upload failures and shader preparation coverage

Tracking: [steamos #78](https://github.com/cat99sky/steamos/issues/78).

## Small runtime fix

GPU ASTC command-recording failure now propagates through `upload_levels` after
the upload batch and scratch resources are cleaned up. The first failed upload
does not publish an uninitialized image to texture lookups. Existing content
remains usable after a failed re-upload. An explicit retry flag preserves the
need to upload even after write-watch consumes the original CPU write. Draw
bindings retry on the next submit rather than once per draw/alias slot.

Deferred mip failures retain the last valid minimum LOD and remain queued. Each
failed entry moves behind its peers and is attempted at most once per frame.
Successful levels alone lower the clamp. The descriptor memo is checked again
after a freshness check, so recovery or a changed clamp cannot reuse the old
descriptor in that lookup. A persistent failure can still leave a dummy or coarse
texture visible; this patch protects validity and retries, not decoder recovery.
The pending queue also retries a failed re-upload without another bind, using
the normal freshness check to refresh the mip tail for the new write generation
before lowering its clamp. The upload census counts only recorded uploads.

The existing ASTC decoder, budget, compression mode, and `0x880` shader key
contract are unchanged. Raw ASTC bytes are never used as an RGBA/BC3 fallback.

## Shader diagnostics

The design follows the opt-in, per-key preparation diagnostics in
[LASRecomp `exec.c`, `LARECOMP_PREP_CHECK`](https://github.com/peiikai/LASRecomp/blob/31f0bfea7e767776c90ceb766269747a87870d18/runtime/nvn/vulkan/exec.c#L6512).
This is a smaller shader-level diagnostic, not LASRecomp's full pipeline diff.
Country-Box already has recorded-input replay and boot warming; this patch reuses
their input identity instead of introducing a second cache or changing keys.

`MP4_TRACE=gfx,perf` adds `gfx_shader: miss`, `translated`, `wait`, and
`wait-failed` lines with the event's starting frame, stage, request key, canonical
key and input ID (when known). A provisional request has `-` for unknown IDs;
the successful translation maps that request to its final IDs. `translated`
measures work after the final cache miss check; `wait` measures the actual worker
wait including lock acquisition/queueing, excluding diagnostic logging. These
are different, potentially overlapping measurements. Ordinary cached lookups
produce no new lines. The existing `gfx` trace flag is off by default.

For an existing cache directory, without starting the game:

```sh
python3 core/tools/shader_coverage.py /path/to/cache-directory
```

It validates both headers and record lengths, then compares **unique input IDs**
from `shader-answers.bin` (216-byte records, ID at offset 36) against
`shader-prepared.bin` (36-byte records, ID at offset 4). A 177,028-byte prepared
file has 4,917 records after its 16-byte header. Recorded IDs outside that list
are historical coverage gaps, not proof of missing SPIR-V, current-frame misses,
or the cause of the reported 747 ms wait. Missing/corrupt files exit with error.

## Validation and remaining acceptance

Offline checks, with a current Clang and Vulkan headers:

```sh
python3 core/tests/shader_coverage_test.py
clang -std=gnu17 -O1 -ffunction-sections -fdata-sections \
  -Icore/runtime -Icore/runtime/third_party/vma \
  core/tests/texture_upload_test.c -Wl,--gc-sections -lpthread -lm \
  -o /tmp/texture-upload-test
/tmp/texture-upload-test
```

The C fixture includes the actual runtime source and replaces only external
GPU/memory/profiling interfaces. It injects decode failure, checks upload cleanup,
first-upload recovery, consumed-write retries through both freshness paths,
retained LOD, and progress past a failed pending texture. It does not exercise
a real GPU or the descriptor memo integration. Compile all three changed runtime
translation units as well (`vk_texture.c`, `vk_exec_draw.c`, `gfx_shader.c`).

No game was launched and no automatic traversal/input replay is part of this
change. Full game linking and real SteamOS visual/frame-time acceptance remain
pending. If needed, acceptance is a short **manual** play session with passive
logging; keep `MP4_SHOT_RING` off when measuring frame times. The patch does not
claim to eliminate the 747 ms shader stall or fill unseen preparation variants.
