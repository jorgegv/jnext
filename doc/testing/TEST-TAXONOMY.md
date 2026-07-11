# Test Taxonomy

Every screenshot/functional row in the regression suite belongs to one of three layers. Each layer probes a different slice of the emulator stack; conflating them during investigation hides where the bug really lives. Use this document to map a failing test back to its layer (and therefore the subsystems whose audit/probes are likely to find the cause).

The taxonomy was formalised after Task 14's [test-strategy analysis](../issues/nextzxos-boot/TASK-14-TEST-STRATEGY-ANALYSIS.md) and the esxdos-shim work that lets game NEX assets boot without the full NextZXOS supervisor (see [feat(esxdos): RST $08 shim](../../src/cpu/z80_cpu.h) and the `--esxdos-stub` flag).

---

## Layer 1 — Full-OS SD boot

**Stack:** TBBlue.fw IPL → NextZXOS supervisor → DivMMC → esxDOS → SD-SPI → every other subsystem reachable from booted firmware.

**What it catches:** supervisor-level bugs, boot-ROM divergences, MMU configuration during init, IM2 / NMI routing in the post-supervisor world, IPL→supervisor handoff, NR-register state coherency across boot.

**Status:** currently blocked at G46(b) — see [G46B-INVESTIGATION-LIVE.md](../issues/nextzxos-boot/G46B-INVESTIGATION-LIVE.md). No regression rows fire this layer end-to-end yet; the 6 SKIPs in the `nmi_test` BOOT group (BOOT-LOOP-01, BOOT-LOGO-01, BOOT-DOT-01, BYPASS-CLI-01, BYPASS-FAT-01, BYPASS-INI-01) are gated on this layer.

**Identifying property:** invocation has `--sdcard <...>` AND no `--load`. The boot-ROM auto-load gate at `Emulator::init` fires only when `cfg.load_file` is empty.

---

## Layer 2 — NEX autoload (real-world games)

**Stack:** NEX loader → DivMMC → esxDOS (or the `--esxdos-stub` shim) → SD-SPI → in-game runtime → game-specific subsystems (Layer 2, sprites, tilemap, Kempston mouse, AY/Specdrum, NR registers).

**What it catches:** runtime FS/driver bugs, video-mode regressions, mouse/keyboard input routing, end-to-end NR-register handling under realistic game traffic. Bypasses the supervisor entirely so it's independent of Layer 1's blockers.

**Status:** active. All four game-smoke rows below pass with `--esxdos-stub`. Reference images regenerate cleanly; cross-run determinism verified at 0 pixel diff.

**Identifying property:** invocation includes `--load test/00regression/nex/<game>.nex`. The boot-ROM auto-load gate is suppressed because `cfg.load_file` is non-empty.

| Row | NEX origin | esxdos-stub | Exercises |
| --- | --- | --- | --- |
| `celeste` | pristine upstream build | yes | Layer 2 256×192 narrow mode, Kempston mouse-or-key driver, esxdos save-state probe |
| `celeste2` | pristine upstream build | yes | second build of same engine, different level layout |
| `beanbros` | pristine upstream build (restored from `f714e3fb`) | yes (defensive; game itself makes no RST $08 calls in the title flow) | tilemap mode, ENTER-key menu navigation |
| `shift` | pristine upstream build | no (game makes no esxdos calls) | text-mode rendering, SPACE-key intro skip |
| `trainyard` | pristine upstream build | yes | Layer 2 320×256 wide mode, Kempston mouse cursor, esxdos save-state probe |
| `odemo` | pristine demo build | no | Layer 2 320×256 bank-stride fix functional regression — NR $12 = 14 exercises sub-banks 2-4 |

---

## Layer 3 — z88dk minimal NEX probes

**Stack:** NEX loader → DivMMC + esxDOS shim → minimal z88dk runtime → discrete probe assertions printed to screen.

**What it catches:** specific protocol checks (esxdos function-code dispatch, file-handle reuse, error-code propagation) with source-level control and assertion granularity that real games don't expose. Sub-2-second headless runs make these the **acceptance tests** for runtime esxdos behaviour.

**Status:** infrastructure in `demo/esxdos_probe/` (Task 15b, commit `936d043e`). Reference probe is `demo/esxdos_probe/esxdos_probe.c` — 102 LOC, exercises `esx_m_dosversion`, `esx_f_open/read/seek/close`. Not yet wired as a regression row; pending integration into `regression_tests.conf` once a stable reference image is locked in.

**Identifying property:** the NEX is sourced from `demo/<probe-name>/<probe-name>.nex` (built by z88dk via the demo Makefile), not from `test/00regression/nex/`. Probes embed their PASS/FAIL output directly in the framebuffer so the screenshot serves as the assertion.

---

## When a regression row fails — which layer?

1. Row has `--sdcard` and no `--load` → **Layer 1**. Suspect supervisor / boot path. Start with [G46B-INVESTIGATION-LIVE.md](../issues/nextzxos-boot/G46B-INVESTIGATION-LIVE.md) and the boot-trace-detective agent.
2. Row has `--load test/00regression/nex/<game>.nex` and `--esxdos-stub` → **Layer 2** (real game). The bug is in a runtime subsystem (video, input, audio, MMU under runtime traffic). Probe with PC histograms / port read traces.
3. Row has `--load demo/<probe>/<probe>.nex` → **Layer 3** (z88dk probe). The probe's own framebuffer output names the failure; start by reading the probe's source.

---

## Why this split matters

Before this taxonomy existed, the G46(b) investigation conflated Layer 1 (supervisor stall) with apparent "boot failures" of Layer 2 game NEXes — they looked similar (black screen, stuck splash) but the actual stack was different (`celeste.nex` couldn't reach RST $20 because esxdos wasn't loaded under `--load`, not because the supervisor was looping). The esxdos shim resolved every Layer 2 stall at once; the Layer 1 G46(b) blocker remained untouched by that fix because it lives in a different layer.

Investigations should declare their layer up-front. A "stuck on splash" report under `--load some.nex` is a Layer 2 issue; under `--sdcard alone` it's Layer 1; investigative effort directed at the wrong layer wastes time.
