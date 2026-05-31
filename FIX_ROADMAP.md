# Xenia Fix Roadmap — Genuinely Fixable Known Issues

Triage of all known issues into what is *actually fixable in xenia's code* vs. what is
not a real bug. Sources: in-tree `FIXME`/`HACK`/`assert` markers + upstream GitHub tracker
(this fork == upstream/master, 0 commits ahead).

## Verification status
An x64 Linux CI build runs on every push to `fix/**` (`.github/workflows/ci-linux.yml`,
using the official xeniaproject/buildenv image). Latest run is green:
- The full `xenia-app` builds Release, so every changed library file compiles and links
  on the target platform (all 16 fixes).
- `xenia-base-tests` passes: 1932 assertions in 51 test cases. This runtime-covers the
  base changes (bit_stream, string/UTF-8, chrono).
- PPC instruction tests (which would runtime-cover mfvscr/mtvscr/vpkpx and the const-prop
  change) cannot run in Linux CI: `gentests` needs a VMX128-capable PPC assembler, which
  only exists as xenia's Windows/Cygwin binutils. Those opcode fixes were verified by
  static analysis against the engine's HIR/backend semantics, not at runtime.
- Linux Debug config is unbuildable upstream (a `_GLIBCXX_DEBUG` ABI mismatch); several
  test executables also have incomplete link dependencies. CI uses Release and builds the
  app plus base tests specifically to avoid these pre-existing breakages.

## Progress (updated as work lands)
Fixed in branch `fix/roadmap-tier1`:
- T1.1 VectorConvertF2I now saturates and flushes NaN, matching the x64 backend (value.cc)
- T1.2 BitStream Peek/Write bounded so reads/writes near the buffer tail no longer run off the end (bit_stream.cc)
- T1.4 to_utf8 transcodes invalid UTF-16 leniently instead of throwing (string.cc)
- T1.5 Processor stack arguments stored as full 64-bit slots (processor.cc)
- T2.1 mfvscr/mtvscr implemented (ppc_emit_altivec.cc)
- T2.2 vpkpx implemented (ppc_emit_altivec.cc)
- T2.5 constant propagation no longer folds rounding-sensitive float ops (constant_propagation_pass.cc)
- T6.2 audio driver freed instead of leaked when init fails / no audio device (sdl + xaudio2 audio systems)
- T7.7 static-analysis defects from the PVS-Studio report (#2290): 9 fixed -
  vulkan render target 64bpp pipeline check, xam enumerate null-check ordering,
  texture_extent self-assign, config catch-by-reference, chrono now() return path,
  descriptor set pool retry continue, filesystem GetInfo memset on path, ImGuiDialog
  virtual destructor, audio driver leak (same as T6.2). Remaining PVS items are
  either non-bugs (idiomatic union punning, intentional register aliasing, harmless
  virtual call in dtor), a false positive (SVOD level1 count is correct per file),
  or no longer present in current master.

Already correct in current master, no change needed:
- T1.3 config BOM skip, T1.7 --help handling, T1.8 per-game config load + callbacks

Deferred (need an x64 build and/or a repro/title to verify; risky to change blind):
- T1.6 dispatch-header assumption, T2.3 lve*x single-element semantics, T2.4 homebrew ELF loader,
  T3.x leaks (error-path / tracing-only / overlapping physical heaps),
  Tier 4 GPU shader accuracy, Tier 5 kernel, remaining Tier 6 input, Tier 7 platform/build.
  These either depend on guest/hardware behavior that can't be confirmed without running the
  emulator, or are conservative-vs-correct trade-offs that need profiling on a real build.

## Hard constraint
This dev machine is **macOS / arm64**. Xenia is **x64, Windows-primary / Linux-secondary**
and **will not build or run here**. Any fix written must be reviewed by reading only; it
cannot be compiled or runtime-verified locally. Real verification needs an x64 Win/Linux box.

---

## EXCLUDED (not real errors / not fixable in xenia)
- **`assert_always` / `assert_unhandled_case` (≈503 sites)** — intentional guard rails for
  unemulated hardware paths. Removing them = silent corruption. Each is only "fixed" by
  implementing the missing behavior (counted elsewhere where concrete).
- **Driver bugs** — #2030 (Nvidia Ampere), #2026 (AMD), #2071 (amdxc64.dll), #2127 (Intel Arc),
  #2093 (Intel stencil). Worked around at best; root cause is vendor drivers.
- **Feature requests** — Kinect (#2347/#2339), VR/OpenXR (#2310), wheels/FFB (#2224),
  Skylanders portal (#2320), Save States (#2276), hotkey-close (#2244). Not bugs.
- **Project/meta** — #2300 (is it maintained?), #2288 (mirror request), #2266/#2241 (packaging),
  #2323/#2269 (dead wiki links), #2268 (unify build system).
- **Per-game RE** — Forza terrain (#1533), Sniper Elite V2 (#2151), Tomb Raider (#2264),
  THPG (#2225), Akai katana stutter (#2254). Each needs frame-trace debugging of that title.

---

## TIER 1 — Concrete isolated code bugs (highest confidence, smallest blast radius)
| ID | Location / Issue | Problem | Notes |
|----|------------------|---------|-------|
| T1.1 | `cpu/hir/value.cc:1068` | Saturation path does not actually saturate | Self-contained math fix |
| T1.2 | #2073 `base` BitStream | offset > (buffer_size-8) → access violation | Add bounds check |
| T1.3 | #1419 cpptoml / config | UTF-8 file with BOM not handled | Strip BOM on read |
| T1.4 | #1780 kernel logging | UTF-8 conversion exception on invalid wide-string args | Guard/replace invalid chars |
| T1.5 | `cpu/processor.cc:383` | Arg marshalling assumes 32-bit args | Handle 64-bit args |
| T1.6 | `kernel/xobject.cc:356,437` | Assumes every object has dispatch header; some don't | Type-check before deref |
| T1.7 | #1516 / #1516 config | Xenia doesn't print help when required | CLI handling |
| T1.8 | #1707 per-game configs | Per-game config sometimes ignored | Config load ordering |

## TIER 2 — Missing CPU (PPC) opcodes (isolated, well-defined, block whole games)
| ID | Opcode | Issue | Notes |
|----|--------|-------|-------|
| T2.1 | `mtvscr` | #2271 (The Orange Box) | Move To Vector Status/Control Reg |
| T2.2 | `vpkpx` | #2035 | Vector Pack Pixel |
| T2.3 | lvex* | #2200 | Load Vector Element Integer Indexed broken |
| T2.4 | misc | #1414 | Homebrew ELFs crash/fail to launch (loader path) |
| T2.5 | const-prop | #1720 | Const propagation ignores FP rounding/flush mode |

## TIER 3 — Memory leaks (real, localized)
| ID | Location | Problem |
|----|----------|---------|
| T3.1 | `gpu/vulkan/vulkan_command_processor.cc:2826` | Leak if guest doesn't clean up |
| T3.2 | `memory.cc:1423/1459/1500` | Leaks parent memory on some paths |
| T3.3 | `cpu/backend/x64/x64_sequences.cc:66` | "don't just leak this memory" |
| T3.4 | #2253 d3d12 | depth_bias_slope_scaled RTV leak/perf |

## TIER 4 — GPU shader-translation accuracy (real correctness bugs, larger/risky)
Texture LOD/mip/gradient/offset shortcuts in DXBC + SPIR-V fetch translators — ~24 paired
FIXMEs. Cause wrong texture filtering. Must be fixed in both backends together.
- `gpu/dxbc_shader_translator_fetch.cc` (lines 148, 409, 640, 728, 790, 816, 938, 947, 1103, 1166, 1414, 1550, 1571, 1617)
- `gpu/spirv_shader_translator_fetch.cc` (lines 126, 230, 563, 741, 804, 837, 1030, 1075)
- `gpu/command_processor.cc:248` — `WAIT_UNTIL` register not processed
- `gpu/dxbc_shader_translator_om.cc:431` — eval_sample_index + SV_Position workaround
- #1511 DXBC translator uses uninitialized registers
- #2012 SV_Barycentrics interpolation (Nvidia Turing+ noise)

## TIER 5 — Kernel / VFS correctness
| ID | Location / Issue | Problem |
|----|------------------|---------|
| T5.1 | #1559 | GPU memcopy not invalidated across different heaps / same phys addr |
| T5.2 | #1946 | XAM XMsgStartIORequestEx must reset XAM_OVERLAPPED event |
| T5.3 | #2154 | DLC / title-update issues |
| T5.4 | #2210 (vfs) | Non-ASCII (CJK) paths can't be drag-dropped |
| T5.5 | `kernel/xobject.cc:79`, `util/object_table.cc:174` | Handle-release status not returned |
| T5.6 | `xboxkrnl_memory.cc:523-526` | Hardcoded fake memory stats (gibbed FIXME) |

## TIER 6 — APU / HID / UI
| ID | Issue | Problem |
|----|-------|---------|
| T6.1 | #1515 | Sounds don't loop seamlessly (apu) |
| T6.2 | #1967 | Crash when there are no audio devices |
| T6.3 | #2239 | One controller detected as two |
| T6.4 | #2274 | Inverted triggers in Gears of War 3 |
| T6.5 | #2138 | SDL2 built without external controller mapping support |
| T6.6 | #1887 | Minimized window breaks Xenia (ui/windows) |
| T6.7 | #2215 | Editing draw_resolution_scale doesn't resize correctly |

## TIER 7 — Cross-platform / build (Linux is second-class)
| ID | Issue | Problem |
|----|-------|---------|
| T7.1 | #2275 | cxxopts won't build on Ubuntu 24.04 |
| T7.2 | #2080 | Linux assertion failure (Ubuntu 22.04) |
| T7.3 | #2297 / #2017 | Linux segfault / SEGV in GuestFunction::Call() |
| T7.4 | #2036 | Linux CI: threading Wait-on-Timer tests fail |
| T7.5 | #1927 | Linux GUI threading issue |
| T7.6 | #2336 | Windows: crash saving to OneDrive path |
| T7.7 | #2290 | Batch of static-analysis findings (triage individually) |

---

## Suggested order of attack
1. **Tier 1 + Tier 3** — small, safe, high-confidence; good warmups, low regression risk.
2. **Tier 2** — missing opcodes; isolated, each unblocks specific games; testable via PPC unit tests in `cpu/ppc/testing`.
3. **Tier 5 / Tier 6** — kernel/IO/input correctness.
4. **Tier 7** — build/platform (needs a Linux box to verify).
5. **Tier 4** — GPU shader accuracy; biggest, riskiest, needs trace-viewer verification. Last.

## Reality check
Even the "fixable" set is dozens of independent investigations, several needing an x64
build environment and specific games/hardware to verify. This is weeks-to-months of focused
work, not a single batch edit. Recommend picking one tier (or one item) at a time.
