# Clang/LLVM as jnext's compiler — evaluation (GH #294)

**Analysis record, 2026-10-02.** It describes the tree at v1.0.71 (`eb8a0481d`) and is not maintained.

**Outcome:**
- GH #294 is closed as "stay on gcc".
- The opt-in clang build knob was evaluated on branch `gh294-clang-eval` and was **not merged**.
- The `script_record_test` fixture fix found here was merged.
- The gcc PGO follow-up is **GH #297** (v1.1).

## TL;DR

- **Recommendation: do not switch, and do not offer Clang as a performance option.**
  - With LTO on both sides, Fedora clang 22 is **slower than gcc 16 on every workload**.
  - fps falls by **5–16%** and cycles:u rise by **5–19%**.
  - ThinLTO is the worse clang variant. Full LTO narrows the gap on copper-demo and beast only.
  - Clang+PGO beats plain gcc+LTO by 4–16%, but **gcc+PGO beats clang+PGO by 8–29%**.
- **Correctness under clang 22 is clean, apart from one test-fixture crash, which is fixed in the branch:**
  - unit 12222/12222 and SDL 11348/11348, both after the fix;
  - FUSE 1356/1356;
  - regression 215/215 with 0 screenshot differences;
  - cli-check and docs-check pass.
- **The codebase needed no source change to build with Clang.**
  - Release builds have 0 warnings at the project's default flags.
  - This is no surprise: macOS already ships an AppleClang build, because `make gui-release` passes `CC=/usr/bin/gcc`, which is clang on macOS.
- **Side finding: gcc PGO.**
  - It gives **+17% to +45% fps** on the trained workloads.
  - The gain holds on four titles the profile never saw: **+17% to +20%**.
  - That is a bigger lever than Task 27's LTO (+34.5% on nextzxos). It was measured here, not adopted, and is followed up in GH #297.

## The evaluation branch (not merged)

The evaluation ran on branch `gh294-clang-eval`, cut from `eb8a0481d` (v1.0.71), with two commits.

**The knob, not merged.**
- `Makefile` + `CMakeLists.txt`, +46 lines.
- It added an opt-in target: `make gui-release-clang [CLANG_LTO=thin|full] [CLANG_CC=… CLANG_CXX=…]`.
  - The target builds into its own `build/gui-release-clang`, linking with lld.
- It also added a CMake option, `JNEXT_CLANG_FULL_LTO`, read only when the compiler id is `Clang`.
- Why it was needed: CMake's IPO for LLVM Clang is always ThinLTO (`-flto=thin`), so full LTO needs a knob.
  - The option sets `CMAKE_{C,CXX}_COMPILE_OPTIONS_IPO` to `-flto=full`.
  - It is set after `check_ipo_supported()`.

**The fixture fix, merged.**
- File: `test/script/script_record_test.cpp`.
- The `Rig` fixture now holds its `Emulator` through a `unique_ptr`, and `emu` is a reference to it.
- No row or assertion changed. See §2.

**Proof that the knob left the default gcc build untouched.** With the knob in the tree, `build/gui-release/jnext` was **byte-identical** to main's binary. It was checked after:
- `make clean && make gui-release`;
- the full gcc gate;
- two cold-ccache rebuilds.

**Reproducing without the knob.**
- The Makefile's `CC`/`CXX` can be overridden, so `make <target> CC=/usr/bin/clang CXX=/usr/bin/clang++` builds any tree with clang.
- Clang's ThinLTO then links through GNU ld and `LLVMgold.so`.
- Full LTO needs the option above, or equivalent `-flto=full` compile and link flags plus `-fuse-ld=lld`.

## Toolchains and host

- **Compilers:**
  - gcc 16.2.1 (Red Hat 16.2.1-2), with GNU ld 2.46.1;
  - clang 22.1.8 (Fedora 22.1.8-4.fc44), with LLD 22.1.8.
- **Build tools:** cmake 4.3.0, ccache 4.12.3.
- **Libraries:** Qt 6.11.2, SDL3 3.4.16.
- **Host:** kernel 7.2.5, Ryzen AI 5 340 (6C/12T; the 4.9 GHz cores are 0, 1 and 3). Everything was measured on core 3.
- **Like-for-like flags.** Both compilers effectively build at **-O3**: the Makefile passes `-O2 -DNDEBUG`, and CMake's Release flags then append `-O3 -DNDEBUG`.
  - gcc: `-flto=auto -fno-fat-lto-objects`, linked with GNU ld.
  - clang: `-flto=thin` or `-flto=full`, linked with lld.
- **Side note (PROVISIONAL, superseded).** Before Fedora clang was installed, a ROCm vendor fork (`clang 20.0.0.rocm`) gave the same picture: thin 0.80–0.91× and full 0.85–0.93× gcc fps. It is not used for the verdict.

## 1. Does it build cleanly with Clang?

**Yes, with no source fixes.**
- Default flags: **0 warnings** from clang 22 on the release tree, the same as gcc.
- No GCC-only attributes or builtins appear in `src/`; a grep for `__builtin`, `__attribute__`, `#pragma GCC` and `__GNUC__` finds none.

**LTO plugin and linker.**
- With lld, ThinLTO and full LTO both build. The knob uses lld.
- Without lld, clang's ThinLTO also links through GNU ld plus `LLVMgold.so`, which Fedora ships in llvm-libs. That is the route `make gui-release CC=/usr/bin/clang …` takes, and the clang test gate below used it.

**Warning delta at `-Wall -Wextra`** (release tree, not shipped flags):

| class | count | detail |
|---|---|---|
| `-Wunused-lambda-capture` | 2 | `preferences_dialog.cpp:185,209` |
| `-Wunused-const-variable` | 1 | `z80n_ext.cpp:37` `FLAG_N` |
| `-Wunused-function` | 1 | `copper.cpp:46` `is_move`; gcc reports only `evaluator.cpp:34` `wrap`, which clang also reports |

- None of these clang-only warnings is a bug.
- GCC-only warnings, for completeness:
  - `-Wimplicit-fallthrough` at `tzx_loader.cpp:565`;
  - enum/non-enum `?:` at `debugger_capture.cpp:211` and `audio_panel.cpp:104`;
  - `-Wignored-qualifiers` at `esp_sntp.cpp:101`;
  - `-Wcomment` in tools.

**Test tree (default flags).** Clang reports 2 warnings that gcc does not:
- `test/layer2/layer2_test.cpp:2321` `-Wconstant-conversion`: `layer2_colour(204 + 0x80)` passes 332 into a `uint8_t`, which becomes 76.
  - Benign. The wrapped value 0x4C is what the hardware's palette-offset nibble add produces (0xC+0x8 mod 16 = 0x4).
  - The intent is implicit, though, so the expected value is right by accident of truncation.
- `test/contention/contention_test.cpp:2914` `-Wformat-truncation`: an 84+ character diagnostic is written into `char buf[64]`.
  - A real but cosmetic defect: a failure message would be truncated. The assertion is unaffected.

## 2. Correctness under clang 22

**How it was run.** Every harness hard-codes `build/`, `build/gui-release` and `build/sdl-release`, but the Makefile's `CC` and `CXX` are overridable. So the full default gate ran with:

`make clean`, then `make <target> CC=/usr/bin/clang CXX=/usr/bin/clang++`

- Every tree was clang-built: `build/`, `gui-release`, `sdl-release` and `sdl-unit-test`. This was checked in each `CMakeCache.txt` and in the binary's `.comment`.
- The release trees used ThinLTO.

| gate | gcc (knob + fix) | clang 22 (knob only, before the fix) | clang 22 after the fix |
|---|---|---|---|
| `make clean && make gui-release` | OK, byte-identical | OK, 0 warnings | — |
| `make unit-test` | 12222/12222 | 12187 pass, **`script_record_test` crashed (rc=139)** | **12222/12222** |
| `make unit-test-sdl` | 11348/11348 | 11313 pass, **`script_record_test` crashed** | **11348/11348** |
| FUSE | 1356/1356 | 1356/1356 | — (unaffected) |
| `JNEXT_TEST_JOBS=4 make regression` | 215/215 | **215/215**, 0 screenshot diffs | — (unit-only fix) |
| `make cli-check` / `make docs-check` | OK / OK | OK / OK | — |
| `make win-release` | OK (on both branch commits) | n/a (MinGW gcc) | — |

**Host load.** The 1-minute load was 7–27 on 12 CPUs throughout, from another project's terragrunt mutation runs. Even so, no timing row failed in any regression run.

**The one finding: `script_record_test` dies from stack overflow under clang.** It is not undefined behaviour and not an emulator bug.
- `Rig` held an `Emulator` by value, and `sizeof(Emulator)` is 1,174,808 bytes.
- Clang inlined the row functions (called through `run_group`'s function pointer) into `main`, giving a **7.05 MB** frame.
- `roundtrip_rows` adds **2.36 MB** for two Rigs.
- The SIGSEGV fires on the call into `Rig::Rig`, with `rsp` 9.4 MB below the stack top, past the 8 MB limit.
- Under gcc, the unfixed suite passes: the gcc gate before the fix reported 12222/12222.
  - gcc's pre-fix frame layout was not captured. Its post-fix largest frame in this suite is 2.35 MB.
  - With the fix, clang's largest frame there is also 2.35 MB.
- After the fix, the clang suite passes 35/35, and the unfixed binary reproduces the crash every time.

**Latent hazard of the same class.** Not fixed here: it is out of scope and does not fail today.
- Many unit suites put several `Emulator`s on one frame, under **both** compilers.
- `esxdos_stub_test`'s `main` is **7.06 MB under gcc** too, about 1.1 MB short of the 8 MB default stack.
- Several others are 3.5–4.7 MB: `extended_nex_test`, `mmu_integration_test`, `script_events_test`, `debugger_backend_test`.
- Any inlining change, from either compiler or a new row, can tip one over.
- Method: the largest `sub $N,%rsp` per function in `objdump -d` of each test binary.

**Output equivalence of every measured binary.** The regression only exercises the ThinLTO clang build, so the other perf variants got a separate check: gcc-lto, c22-thin, c22-full, gcc-pgo and c22-pgo produce **byte-identical screenshots** on beast, copper-demo, the nextzxos boot, bifrost and dma-loop.

## 3. Performance

**Method.**
- `--benchmark N` runs headless.
- Each run is `taskset -c 3`, wrapped in `perf stat -e instructions:u,cycles:u`, and gets a private reflinked SD clone.
- Every round runs every workload once per variant, back to back, with the variant order rotated each round.
- Ratios are **paired per round** against gcc-lto, reported as median [IQR].
- Workloads: the five `make bench` workloads plus **dma-loop**, a 29-byte raw binary written for this evaluation (source in the appendix; `--machine 48k --inject dmaloop.bin`, 300 frames).
  - It sets 28 MHz, then reprograms the zxnDMA for back-to-back continuous-mode 6912-byte ROM→screen copies, forever.
  - A screenshot confirms the transfers land.
- Frames per run: 600 for the 48K workloads, 400 for the Next ones, as `make bench` uses (300 for dma-loop).
- The SD image is the standard provisioned `cspect-next-1gb-fixed.img`.
- Run wrapper, per run:
  ```
  taskset -c 3 perf stat -x, -e instructions:u,cycles:u -- jnext --headless --machine M --sdcard CLONE --benchmark N --benchmark-label W [--load F | --inject F]
  ```
  - wall, fps and T-states/s come from the `BENCH` line;
  - instructions and cycles come from `perf stat`.

**Host load was high** (median 1-minute load 11.4 on 12 CPUs, from another project).
- Absolute fps and cycles are therefore depressed, roughly 2× against a quiet box, and their IQRs are wide. **Read the absolute columns as noisy.**
- The paired ratios are tight (typical IQR 0.02–0.04), and they replicated: an independent second 10-round run agrees with this one within about 0.02 on every ratio.

**What each metric measures.**
- `cycles:u` covers the whole process. That includes compiler-independent startup: dynamic symbol binding, plus a libcrypto SHA-256 of the 1 GB SD image that `Emulator::ensure_warm_start_state` runs on every NEX load (~2.6 G cycles on beast and copper).
- The **BENCH wall and fps** time only the frame loop, so they are the cleaner verdict. Cycles understate the code gap slightly.

### 3.1 Primary run (15 rounds, 450 runs, load median 11.36, range 2.46–14.27)

| workload | variant | cycles:u (M) med [IQR] | wall s med [IQR] | fps med [IQR] | instr:u (M) | cycles ratio | wall ratio | fps ratio |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| boot-48k | gcc-lto | 4321 [589] | 1.183 [0.209] | 507.3 [110.9] | 14541 | — | — | — |
| boot-48k | c22-thin | 4955 [647] | 1.395 [0.240] | 430.2 [92.3] | 18410 | 1.136 [0.030] | 1.168 [0.037] | 0.856 [0.027] |
| boot-48k | c22-full | 4865 [650] | 1.351 [0.233] | 444.2 [92.0] | 18246 | 1.132 [0.026] | 1.163 [0.046] | 0.860 [0.034] |
| boot-48k | gcc-pgo | 3386 [406] | 0.899 [0.144] | 667.7 [130.1] | 13797 | 0.786 [0.015] | 0.773 [0.019] | 1.293 [0.031] |
| boot-48k | c22-pgo | 4110 [385] | 1.103 [0.148] | 544.0 [84.2] | 17903 | 0.950 [0.022] | 0.950 [0.032] | 1.053 [0.036] |
| bifrost | gcc-lto | 4251 [586] | 1.156 [0.233] | 519.2 [147.7] | 14653 | — | — | — |
| bifrost | c22-thin | 4924 [667] | 1.364 [0.248] | 439.9 [94.4] | 18584 | 1.164 [0.031] | 1.183 [0.054] | 0.846 [0.038] |
| bifrost | c22-full | 4936 [850] | 1.370 [0.318] | 437.9 [131.5] | 18418 | 1.185 [0.040] | 1.191 [0.056] | 0.839 [0.040] |
| bifrost | gcc-pgo | 3373 [526] | 0.911 [0.200] | 658.7 [199.0] | 13948 | 0.805 [0.016] | 0.793 [0.029] | 1.261 [0.046] |
| bifrost | c22-pgo | 4019 [533] | 1.086 [0.218] | 552.4 [147.5] | 18044 | 0.958 [0.036] | 0.958 [0.040] | 1.044 [0.044] |
| copper-demo | gcc-lto | 18490 [2331] | 5.647 [0.967] | 70.8 [15.6] | 65951 | — | — | — |
| copper-demo | c22-thin | 21882 [3169] | 6.754 [1.253] | 59.2 [15.2] | 82182 | 1.181 [0.025] | 1.182 [0.035] | 0.846 [0.025] |
| copper-demo | c22-full | 20037 [3019] | 6.123 [0.957] | 65.3 [12.1] | 73711 | 1.095 [0.015] | 1.084 [0.017] | 0.922 [0.015] |
| copper-demo | gcc-pgo | 15497 [2095] | 4.733 [0.966] | 84.5 [22.4] | 64487 | 0.844 [0.024] | 0.852 [0.043] | 1.174 [0.061] |
| copper-demo | c22-pgo | 16650 [1962] | 5.023 [0.962] | 79.6 [19.2] | 69685 | 0.903 [0.039] | 0.892 [0.058] | 1.122 [0.070] |
| beast | gcc-lto | 17277 [2615] | 5.291 [1.128] | 75.6 [22.3] | 59354 | — | — | — |
| beast | c22-thin | 19894 [3119] | 6.013 [1.223] | 66.5 [17.6] | 73811 | 1.146 [0.030] | 1.149 [0.037] | 0.870 [0.029] |
| beast | c22-full | 18451 [2725] | 5.636 [1.276] | 71.0 [20.6] | 66859 | 1.070 [0.013] | 1.053 [0.039] | 0.949 [0.035] |
| beast | gcc-pgo | 14047 [1796] | 4.284 [0.913] | 93.4 [26.0] | 56386 | 0.818 [0.019] | 0.817 [0.035] | 1.223 [0.053] |
| beast | c22-pgo | 15445 [2337] | 4.653 [1.168] | 86.0 [27.6] | 62973 | 0.903 [0.041] | 0.898 [0.056] | 1.114 [0.071] |
| boot-nextzxos | gcc-lto | 8047 [1776] | 2.313 [0.732] | 173.0 [74.6] | 25376 | — | — | — |
| boot-nextzxos | c22-thin | 9378 [1985] | 2.698 [0.829] | 148.2 [63.6] | 30444 | 1.163 [0.027] | 1.165 [0.050] | 0.858 [0.037] |
| boot-nextzxos | c22-full | 9390 [2181] | 2.737 [0.836] | 146.1 [62.6] | 29825 | 1.150 [0.021] | 1.164 [0.035] | 0.859 [0.027] |
| boot-nextzxos | gcc-pgo | 6376 [1174] | 1.850 [0.566] | 216.2 [90.4] | 24230 | 0.799 [0.035] | 0.793 [0.047] | 1.261 [0.075] |
| boot-nextzxos | c22-pgo | 6896 [1457] | 1.958 [0.630] | 204.3 [86.5] | 28340 | 0.867 [0.018] | 0.859 [0.039] | 1.164 [0.051] |
| dma-loop | gcc-lto | 5128 [1289] | 1.433 [0.531] | 209.3 [113.3] | 17920 | — | — | — |
| dma-loop | c22-thin | 5435 [1333] | 1.552 [0.540] | 193.3 [97.4] | 21128 | 1.065 [0.042] | 1.083 [0.045] | 0.924 [0.039] |
| dma-loop | c22-full | 5414 [1353] | 1.542 [0.490] | 194.5 [88.8] | 21082 | 1.053 [0.034] | 1.066 [0.046] | 0.938 [0.041] |
| dma-loop | gcc-pgo | 3597 [841] | 0.982 [0.338] | 305.3 [142.9] | 15906 | 0.717 [0.026] | 0.690 [0.031] | 1.450 [0.066] |
| dma-loop | c22-pgo | 4802 [934] | 1.337 [0.334] | 224.3 [72.4] | 20837 | 0.952 [0.061] | 0.956 [0.069] | 1.045 [0.074] |

**Head-to-head PGO tier** (replication run): c22-pgo fps relative to gcc-pgo.

| boot-48k | bifrost | copper-demo | beast | boot-nextzxos | dma-loop |
|---:|---:|---:|---:|---:|---:|
| 0.825 | 0.836 | 0.922 | 0.909 | 0.912 | 0.710 |

### 3.2 PGO generalisation (8 rounds; workloads absent from the training set; load median 7.6)

fps ratio vs gcc-lto, median [IQR]:

| workload | c22-full | gcc-pgo | c22-pgo |
|---|---:|---:|---:|
| celeste | 0.951 [0.069] | 1.203 [0.223] | 1.122 [0.239] |
| santaspressie | 0.929 [0.064] | 1.173 [0.080] | 1.130 [0.102] |
| odemo | 0.903 [0.070] | 1.168 [0.152] | 1.064 [0.098] |
| test02layer2 | 0.890 [0.128] | 1.198 [0.048] | 1.126 [0.075] |

Cycles ratios for the same runs:
- gcc-pgo: 0.83–0.88;
- c22-full: 1.08–1.10.

**PGO training and its caveat.**
- Training set: the six benchmark workloads at half the frames, plus `parallax.nex` and `trainyard-express.nex`. All are in `test/00regression/`.
- PGO builds, both with LTO as above:
  - gcc: build with `-fprofile-generate -fprofile-update=single`, train, then rebuild in the same build dir with `-fprofile-use -fprofile-correction -Wno-missing-profile`.
  - clang (full LTO): build with `-fprofile-instr-generate=DIR/%m-%p.profraw`, train, run `llvm-profdata merge`, then rebuild with `-fprofile-instr-use=FILE`.
- The trained-workload PGO numbers above are therefore best-case.
- §3.2 is the honest estimate for unseen software: about +17% to +20% for gcc.

### 3.3 Where clang loses

Measured with `perf record -e instructions:u` (deterministic counts, not timing), with symbols bucketed by subsystem. Instructions in G, gcc-lto → c22-full:

| workload | total | render | CPU + memory + ports | devices | startup/libs |
|---|---|---|---|---|---|
| boot-nextzxos | 25.4 → 29.8 | **3.5 → 5.2 (+49%)** | 12.7 → 14.1 (+11%) | 8.1 → 9.3 (+14%) | 0.61 = 0.61 |
| bifrost | 10.0 → 12.5 | **3.6 → 5.3 (+47%)** | 2.5 → 2.7 (+8%) | 3.1 → 3.5 (+14%) | 0.61 = 0.61 |
| beast | 59.4 → 66.9 | **7.9 → 10.4 (+32%)** | 19.5 → 21.4 (+10%) | 27.9 → 30.4 (+9%) | 3.47 = 3.47 |

- **The compositor is the biggest single gap.**
  - On bifrost, gcc's `composite_scanline`, `apply_ula_clip`, `Tilemap::render_scanline` and `Ula::render_display_line` total about 3.4 G instructions.
  - Clang inlines all of them into `Renderer::render_row`, which runs 4.86 G.
- The interpreter and device loop runs 8–14% more instructions.
- Clang's IPC is higher, which recovers part of the instruction excess. That is why the cycles ratio (≈1.07–1.19) is smaller than the instruction ratio (≈1.12–1.27).
- **Not established:** the codegen reason (branch shape, vectorisation or inlining choices) behind the +47% in the compositor. Only where the extra work is was measured.

### 3.4 Build time and size

**Build time.** Wall time of configure + build of the Release GUI tree, `-j12`, cold = empty private `CCACHE_DIR`. There were two interleaved repetitions at load 11–17, so these figures are noisy:

| | gcc LTO | clang ThinLTO | clang full LTO |
|---|---:|---:|---:|
| cold ccache | 75.6 / 70.4 s | 61.0 / 60.2 s | 88.9 / 87.6 s |
| warm ccache | 26.1 / 23.8 s | 16.5 / 16.1 s | 42.7 / 42.2 s |
| ccache footprint | 49 MB | 15 MB | 15 MB |

- An earlier single run at lower load (4.8–9) ranked them the same: gcc 55.7 s cold / 16.6 s warm, clang thin 48.7 / 13.2 s, clang full 70.7 / 49.6 s.
- Warm builds are dominated by the LTO link. ThinLTO's parallel link is the only build-time win.

**Size:**

| | gcc-lto | c22-thin | c22-full | gcc-pgo | c22-pgo |
|---|---:|---:|---:|---:|---:|
| file (bytes) | 5,005,616 | 5,182,584 | 4,869,984 | 3,687,840 | 4,237,560 |
| `.text` (bytes) | 4,145,022 | 4,326,644 | 4,182,673 | 2,810,273 | 3,254,127 |

## 4. Platform and packaging impact

Nothing in this section was changed; it is a report only.

- **macOS:** already AppleClang. `make gui-release` passes `CC=/usr/bin/gcc`, which is the clang shim on macOS. The new option ignores AppleClang, which has its own compiler id.
- **Windows** (`make win-release` / `package-win*`: Fedora MinGW gcc cross, in CI too):
  - Fedora 44 packages **no** clang→MinGW cross toolchain. `mingw64-llvm` 21.1.8 is LLVM *libraries* for the target, not a compiler.
  - llvm-mingw (external) is libc++-based, so it cannot link against Fedora's libstdc++-built mingw Qt6/SDL3 DLLs.
  - `clang --target=x86_64-w64-mingw32` against the gcc sysroot is conceivable, but unpackaged and untested.
  - **Windows would stay on gcc**, so a switch means three compilers across three platforms.
- **RPM** (`packaging/rpm/jnext.spec`): `BuildRequires: gcc-c++`. A switch needs `clang` and `lld` added, plus `%global toolchain clang`, which Fedora supports.
- **DEB** (`packaging/debian/control`, `release.yml` on ubuntu 24.04/26.04): `g++` would become `clang` + `lld`.
  - Each distro ships its own clang major (24.04's default is older than 22; exact versions not verified), so performance would vary by distro.
- **Flatpak** (`org.kde.Sdk` 6.10): builds with the SDK's gcc. Clang would need an `org.freedesktop.Sdk.Extension.llvm*` SDK extension; one is believed to exist, not verified.
  - One upside, untested: the Flatpak currently runs **without LTO** because the KDE SDK gcc miscompiles under LTO (`doc/issues/FLATPAK-LTO-PROBLEMS.md`). Clang+LTO might give that build its LTO back.
- **CI** (`ci.yml`, `container: fedora:44`): installs `gcc-c++ ccache`. A clang leg adds `clang lld`.
- **Makefile and harness:** the Makefile hard-codes `CC := /usr/bin/gcc`, and `cmake-configure-guard-selftest.sh` uses gcc. Both are harmless, and both can be overridden.
- **ccache:** no gcc assumption; it works with clang unchanged.
- **Cost of a switch:**
  - packaging edits in about four places (spec, debian, flatpak, release.yml) plus CI;
  - a compiler split with Windows;
  - **a 5–16% slower product**.
- **Benefit of a switch:** none measured.

## 5. Recommendation

1. **Do not switch the default compiler.** gcc 16 + LTO beats clang 22 + LTO (ThinLTO or full) on all six workloads, by 5–16% fps. It also beats clang on all four untrained titles.
2. **Do not offer clang as a performance option.** There is no configuration where it wins: even with PGO, clang trails gcc+PGO by 8–29%.
3. **Merge the `script_record_test` fix on its own merits.** (Done.)
   - It is a compiler-agnostic fixture fragility, and clang merely exposed it.
   - It needs independent review first.
   - The neighbouring multi-MB stack frames (esxdos_stub_test at 7.06 MB under gcc) are the same hazard. They were left as they are.
4. **The `gui-release-clang` knob is optional.** It is useful only as a portability probe, and macOS's AppleClang release leg already covers most of that. (Not merged.)
5. **Consider gcc PGO separately, outside #294.** (Now GH #297.)
   - Gain: +17% to +45% fps on the benchmark set, and +17% to +20% on untrained titles.
   - This is the largest lever measured since LTO.
   - Its costs are real and unmeasured here:
     - a training step in every release build, including packaging, Flatpak, the Windows cross build and CI;
     - profile staleness;
     - build reproducibility;
     - whether the gain holds in the GUI binary under real use.

Effort and risk of a switch, had it been chosen anyway:
- Effort: about 1 day of packaging and CI edits, plus deb/rpm/flatpak verification.
- Risks:
  - distro clang versions differ;
  - Windows stays on gcc;
  - stack-heavy test fixtures are inlining-sensitive (shown above).

## 6. Artefacts and reproduction

**The measurement artefacts were kept locally and are not committed.** They are:
- the binaries;
- the per-run TSVs;
- the gate logs;
- the perf profiles;
- the profile data.

Everything above can be re-derived from this text:
- the workloads, frame counts, core pinning and `perf stat` events in §3;
- the PGO flags and training set in §3.2;
- the gate commands in §2.

One rule of the method matters: interleave the variants, rotate their order each round, and take per-round paired ratios. That is what makes the result robust to host load.

## Appendix: dma-loop source

Assemble with `z88dk-z80asm -b -o=dmaloop.bin dmaloop.asm`.

```
; Back-to-back zxnDMA memory->memory block copies: 6912 bytes of ROM ($0000)
; to the screen ($4000), continuous mode, forever. Interrupts disabled.
        org  $8000
DMA     equ  $6B
start:
        di
        ld   sp, $BF00
        ld   bc, $243B          ; NR $07 = 3: 28 MHz, so more DMA per frame
        ld   a, $07
        out  (c), a
        inc  b
        ld   a, $03
        out  (c), a
loop:
        ld   hl, prog
        ld   b, prog_end - prog
        ld   c, DMA
        otir
        jr   loop
prog:
        defb $C3                ; WR6 reset
        defb $7D                ; WR0 A->B, A start + length follow
        defw $0000              ; port A start (ROM)
        defw $1B00              ; block length 6912 (a whole screen)
        defb $14                ; WR1 port A memory, increment
        defb $10                ; WR2 port B memory, increment
        defb $AD                ; WR4 continuous mode, B start follows
        defw $4000              ; port B start (screen)
        defb $82                ; WR5 stop at end
        defb $CF                ; WR6 LOAD
        defb $87                ; WR6 ENABLE
prog_end:
```
