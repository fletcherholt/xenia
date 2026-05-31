# Xenia Performance Roadmap

Why some games (e.g. Skate 3 on Steam Deck) run sluggish, and where the
emulator can be optimized. Findings are from reading the code + `docs/cpu_todo.md`.
File references are `path:line` into this tree.

## Context: Steam Deck + Skate 3
- Deck is x86-64 Zen 2 + RDNA2 (AMD). So the **x64 JIT** and the **Vulkan / D3D12**
  backends are what matter (not the ARM/Android paths).
- Two ways xenia runs on Deck:
  - **Windows build under Proton** -> D3D12 backend translated to Vulkan by
    vkd3d-proton. Adds a D3D12->Vulkan translation layer on top of emulation.
  - **Native Linux build** -> Vulkan backend directly. Linux support is marked
    "experimental and presently incomplete" (`docs/building.md`), and the Debug
    config doesn't even link, but Release works (see FIX_ROADMAP / CI).
- On AMD, the high-accuracy render path is effectively unavailable: the D3D12 ROV
  path "currently causes shader compiler crashes in many cases" on AMD drivers
  (`d3d12_render_target_cache.cc:50`), and both ROV (D3D12) and FSI (Vulkan) are
  "much slower now" than the default RTV/FBO path. So the Deck is locked to the
  copy-on-layout-change render path, whose cost is the central GPU bottleneck.

---

## TIER A - Settings / tuning (no code, do first, verify on Deck)
These are the highest-leverage things to try right now for Skate 3.

| Setting (config.toml) | Recommendation | Why |
|---|---|---|
| `draw_resolution_scale_x/y` | keep at **1** on Deck | cost is ~quadratic; 2x = 4x the pixels. Defined `texture_cache.cc:29/42`. |
| `render_target_path_*` | leave default (RTV/FBO) | ROV/FSI are slower and ROV crashes on AMD. `render_target_cache.cc` / backend flags. |
| `vsync` | test `false` for uncapped, `true` to stop tearing | `gpu_flags.cc`. Uncapped shows true CPU/GPU headroom. |
| `texture_cache_memory_limit_soft/hard` | tune to Deck's unified RAM (defaults 384/768 MB) | thrashing/eviction if too low for the game's working set. `texture_cache.cc:47+`. |
| `d3d12_readback_resolve` / `d3d12_readback_memexport` | keep **false** unless a game needs them | each forces a GPU->CPU sync stall. `d3d12_command_processor.cc:36/43`. |
| Native Vulkan build vs Proton/D3D12 | benchmark both | native Vulkan removes the vkd3d-proton translation layer; Proton's D3D12 path is more mature. |
| Shader storage / precompile | ensure enabled | avoids first-encounter pipeline-compile hitching (see Tier B.6). |

Action: capture a Skate 3 frame-time trace at each setting to find which subsystem
is actually the limiter (CPU-bound vs GPU-bound) before touching code.

---

## TIER B - GPU optimizations (most likely the Skate 3 limiter)
1. **EDRAM render-target copying / barriers (RTV/FBO path).** The default path's
   performance is "limited primarily by render target layout changes requiring
   copying" (`render_target_cache.cc` flag docs). Every time the guest repoints
   EDRAM, host render targets may be copied and barriers inserted. Reducing
   redundant copies/barriers when EDRAM ownership changes is the single biggest
   GPU win. See `execute_unclipped_draw_vs_on_cpu...` note (`render_target_cache.cc:186+`)
   already added to cut "excessive barriers".
2. **Pipeline / shader compilation stutter.** Host pipeline state objects are
   created on demand; first encounter of a shader/state combo hitches. Verify
   shader-storage precompilation (`emulator.h` `on_shader_storage_initialization`,
   `pipeline_cache.cc`) is active and consider async pipeline creation so draws
   don't block on compilation.
3. **memexport readback.** GPU-written data read back to shared memory forces
   sync. If Skate 3 uses memexport, this serializes CPU/GPU. Investigate
   `dxbc_shader_translator_memexport.cc` / `spirv_shader_translator_memexport.cc`
   and the readback in `d3d12_command_processor.cc:2422+`.
4. **Resolve cost (EDRAM -> texture).** Resolves run compute shaders
   (`shaders/.../resolve_*`). Frequent small resolves add up; batch/skip
   redundant resolves where the destination is unused.
5. **draw_util "slow path".** When render-target components are disabled, a slow
   read-merge path is taken (`draw_util.cc:642`, `draw_util.h:310`). Worth
   checking how often Skate 3 hits it.
6. **Texture cache thrash.** Soft/hard memory limits with time-based eviction
   (`texture_cache.cc:47+`); on the Deck's shared memory, an undersized budget
   causes reload churn. Tune limits and check load/upload cost in
   `shared_memory.cc`.
7. **Accuracy-vs-speed format paths** (`gamma_render_target_as_unorm16`,
   `snorm16_render_target_full_range`, `depth_float24_convert_in_pixel_shader`):
   each adds shader/format work. Expose/measure cheaper modes where the game
   tolerates them.

## TIER C - CPU / JIT optimizations (from docs/cpu_todo.md + x64 backend)
1. **Emulated opcodes via guest->host thunks.** ~36 `CallNativeSafe` sites and
   `Emulate*` fallbacks in the x64 backend (`x64_sequences.cc`, `x64_seq_vector.cc`):
   `EmulateFLOAT`, `EmulateVectorShr/Shl/RotateLeft`, `EmulatePow`, `EmulateLog`,
   `EmulatePack`, `EmulateVectorAverage`. Each is a full context transition
   (hundreds of host instructions). Replacing with native AVX/AVX2 is the biggest
   codegen win (`cpu_todo.md` "Implement Emulated Instructions").
2. **No code-cache serialization.** The x64 code cache is re-JITed every launch
   (`cpu_todo.md` "Serialize Code Cache"). Persisting it to disk removes startup
   JIT cost and first-run stutter.
3. **RegisterAllocationPass is the slowest pass** (`cpu_todo.md`). Faster
   allocation / use-tracking speeds every translation.
4. **Constant pooling missing.** Non-trivial vec128 constants emit 20-30 byte
   loads, bloating I-cache (`cpu_todo.md`). RIP-relative constant table instead.
5. **Few usable x64 registers** -> spills in hot functions (`cpu_todo.md`
   "Increase Register Availability").
6. **Missing optimization passes** (`cpu_todo.md`): cross-block constant
   propagation, dead-store elimination, type propagation, X64 canonicalization,
   merge-local-slots. Each reduces emitted code size / spills.

## TIER D - Memory / synchronization
1. **Access-violation-based write watches.** Texture/render-target/shared-memory
   invalidation is driven by page-protection traps (`memory.cc:440`
   `AccessViolationCallback`, `mmio_handler.cc:389+`). Every guest write to a
   watched page faults into a handler. Streaming-heavy games (open-world like
   Skate 3) can generate many traps; coarser/lazier watch granularity or
   batching invalidations could help.
2. **MMIO handler exception cost** on the trap path (`mmio_handler.cc`).

## TIER E - Skate 3 specific investigation
1. Capture a GPU trace (F4 / `--trace_gpu_prefix`) during a sluggish moment and
   open it in the trace viewer to see draw/resolve/copy counts per frame.
2. Determine CPU-bound vs GPU-bound: run `--vsync=false` and watch whether frame
   time scales with resolution scale (GPU-bound) or not (CPU-bound).
3. Check whether Skate 3 relies on memexport or frequent readbacks (Tier B.3).
4. Compare native Vulkan vs Proton/D3D12 frame times on the same scene.

---

## Constraints (same as FIX_ROADMAP)
- Cannot build/run on the dev box (macOS/arm64); xenia is x64. Code changes are
  verified via the fork's x64 CI; runtime perf must be measured on the Deck.
- Tier A is doable immediately by Fletcher on the Deck. Tiers B-D are real
  engineering with measurable wins but need profiling on target hardware to
  prioritize - do not optimize blind.
