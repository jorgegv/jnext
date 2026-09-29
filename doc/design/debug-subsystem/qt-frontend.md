# Qt GUI frontend — inventory, projection and refactor plan (GH #278)

Status: **v6 — round-4 review 2026-09-27 (`scratchpad/reviews/dsl-qt-r4.md` R-3, N-10; dispositions §11). Previously: v5 — owner review 2026-09-27 (Q5, Q7, Q13 decided; WP8 added; CAP-INS-02 `MemSpace::Page` now used). Previously: v4 — revised after the independent review of 2026-09-26 (`scratchpad/reviews/dsl-qt.md`, items R-6, R-7, N-9, N-10, N-11; dispositions in §11). Previously: v3 (final for the design round) — inventory complete; projection mapped onto `backend.md` v1 plus design-backend's verdicts of 2026-09-26 (all 14 sub-REQs ACCEPTED/CONFIRMED, §8). Re-verified against `backend.md` **v3**: every CAP id cited in §3.2 exists there, and all REQ-qt verdicts are recorded in its §12. MAPPED after the owner review and round 4: **40 used** (CAP-INS-02 `MemSpace::Page` AND `MemSpace::Rom`, both by WP8), **15 declined**, 0 open, 0 reach-arounds — confirmed against backend v7.**
Owner of this file: the `design-qt` agent. Sibling files: `backend.md`,
`dsl.md`, `dzrp.md`, `zrcp.md`, `gdb.md` (read, never edited from here).

Everything below is measured against the code at `gh276-design` =
`main` @ `974b0ab19` (v1.0.44). File:line citations are into that tree.
`doc/design/EMULATOR-DESIGN-PLAN.md` §5.10 was not consulted as a source.

---

## Work packages — the tracker for this package

Mirrors this package's row in [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
§10.1, which stays authoritative: if the two ever disagree, §10.1 wins and this
table is stale. It exists because §10.1 states each package's sequence as one
long table cell, which is unreadable as a plan and impossible to track against.

Status values: `todo` · `in progress` · `in review` · **`done`** (independently
reviewed and APPROVED). The whole package lands on **one branch** and merges
whole, so `done` here means the sub-item is approved, not merged.

| WP | Branch `gh278-qt` (issue #278) | Status |
|---|---|---|
| **WP0** | close the identity gaps on the **current** tree, so the suites are green on both trees by construction — as built: §6.2a | **done** — reviewed + APPROVED (rows, then the fix round); landed on `main` alone |
| **WP1** | the `src/qt/` header move + `make build-matrix`. **Q is the single owner of this move**, and it lands with the rest of Q on Q's one branch — as built: §6.2b | **done** |
| **WP2** | `DebuggerManager` verbs onto the backend facade — as built: §4.1, §6.2b | **done** |
| **WP3** | rewind / trace / corruption — as built: §6.2c | in review |
| **WP4a-d** | the panels (parallel-able). **WP4d also owns the `render_layer` MOVE itself**, not only its 106 DVP validation rows — owner decision 2026-09-27, closing a gap §10.1 left unassigned | todo |
| **WP5** | memory panel | todo |
| **WP6** | symbols / magic | todo |
| **WP7** | reach-around grep = 0 (`grep -l 'core/emulator.h' src/debugger/*.cpp` empty) | todo |
| **WP8** | **Memory panel physical-page view** — `MemSpace::Page` reads *and* writes (owner decision §1.3 item 15). **Last**, after the identity rows are green, with its own pinned rows | todo |

Depends on: B0 (landed), B. Q is the epic's **sufficiency proof** — any
insufficiency it finds is a finding against the architecture document, not a
side channel.

Every sub-item is reviewed by an agent or person that did NOT write it, and the
branch does not merge until the full §10.3 gate is green on the tip.

---

## 0. Corrections to the brief

The brief is a hypothesis; three of its numbers are wrong or ambiguous.

| Claim | Measured | Where |
|---|---|---|
| "33 raw `Emulator*` pointers in the panel headers" | **33 textual occurrences**, but in **15 headers**, not "panel headers": 13 panel classes × 2 (ctor parameter + member) = 26, `VideoLayerView` × 2 = 2 (`video_panel.h:32,67`), `DebuggerWindow` × 2 (`debugger_window.h:36,113`), `DebuggerManager` × 3 (`debugger_manager.h:23,64,117`). So the number is right; the description is not. Counting POINTER MEMBERS: 16 (14 widgets + window + manager). | `grep -c 'Emulator\s*\*' src/debugger/*.h` |
| "14 panels" | **13 panel classes** are created by `DebuggerWindow::create_panels()` (`debugger_window.cpp:1032-1079`) and refreshed by `refresh_panels()` (`:1220-1234`). The 14th `QWidget` holding an `Emulator*` is `VideoLayerView`, a sub-widget the Video panel instantiates six times (`video_panel.cpp:888-950`). The developer guide says thirteen (`src/doc/developer-guide/03-subsystems/09-debug-and-the-debugger.md`, "Panels"). Both counts are used below with their meaning stated. | |
| "Inspection is 33 raw pointers" | The pointers are the *entry*; the inspection surface is what the `.cpp` files reach THROUGH them: **21 distinct core objects** (`Z80Cpu`, `Mmu`, `Ram`, `NextReg`, `SpriteEngine`, `Copper`, `TurboSound`/`AyChip`, `Renderer`, `Ula`, `Layer2`, `Tilemap`, `PaletteManager`, `VideoTiming`, `Timing` struct, `Clock`, `RewindBuffer`, `TraceLog`, `CallStack`, `DebugState`, `BreakpointSet`, `RzxPlayer`/`RzxRecorder`) plus 16 `Emulator` members/methods called directly. The full list is §1. | §1 |

One more thing the brief does not say: the GUI holds **two pieces of
debugger state that the core does not know about** — the Watches list
(`watch_panel.h:46-52`, panel-owned `std::vector<WatchEntry>`) and the
symbol table (`debugger_manager.h:131`, owned by the Qt manager, handed to
three panels by pointer at `debugger_manager.cpp:210,217,223`). Both have to
find a home in the refactor (§3.5, §3.6).

---

## 1. Inventory — every core access, per panel and per verb

Legend for the **When** column: `P` = only while paused (the panel gates on
its own `paused_` flag), `T` = on every manager tick that reaches
`DebuggerWindow::refresh_panels()` (paused: every tick; running: every 12th
tick, `debugger_manager.h:136-138`, `.cpp:672-679`), `E` = on a user edit/click,
`O` = on `BreakpointSet` observer notification, `C` = at construction,
`X` = on Qt paint (not tied to refresh at all).

### 1.1 CpuPanel — `src/debugger/cpu_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 1 | `cpu().get_registers()` | AF BC DE HL AF' BC' DE' HL' IX IY SP PC I R IFF1 IFF2 IM `halted` — the whole `Z80Registers` struct, six F bits decoded | P | 199-245 |
| 2 | `mmu().port_7ffd()` bit 3 | ULA shadow-screen select → "Bank 5"/"Bank 7" | P | 249 |
| 3 | `renderer().ula().get_screen_mode_reg()` bits 2:0 | Timex mode → "Alt"/"HiCol"/"HiRes" suffix (decode table duplicated from `Ula::set_screen_mode`, :255-263) | P | 250 |

Gate: `if (!paused_) return;` (:197). Grey overlay painted while running (:187-193).

### 1.2 MmuPanel — `mmu_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 4 | `mmu().get_effective_page(s)` ×8 | physical 8K page per slot (NR 0x50-57 or legacy-derived) | T | 116 |
| 5 | `mmu().is_slot_rom(s)` ×8 | ROM/RAM type per slot; RAM rows show `B<page/2>` | T | 117 |
| 6 | `mmu().port_7ffd()` | bits 2:0 bank, bit 4 ROM select, bit 5 paging lock | T | 130-133 |

No paused gate — refreshes while running (throttled).

### 1.3 StackPanel — `stack_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 7 | `cpu().get_registers().SP` | SP | P | 61 |
| 8 | `mmu().read(addr)` ×48 | 24 words at SP, SP+2 … (CPU address space, wrap-guarded) | P | 81-82 |

Gate `:59`.

### 1.4 CallStackPanel — `callstack_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 9 | `call_stack().frames()` | every `CallFrame` (caller_pc, target_pc, sp_at_call, type ∈ CALL/RST/INT/NMI), newest first | P | 49 |
| 10 | `SymbolTable::lookup(target_pc)` | name for the target column | P | 84 |

Gate `:47`. `call_stack().set_enabled()` is driven by the manager (§1.15 #72).

### 1.5 SpritePanel — `sprite_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 11 | `sprites().get_sprite_info(i)` ×128 | `SpriteInfo`: x, y, pattern, palette_offset, visible, x_mirror, y_mirror, rotate, x_scale, y_scale (`sprites.h:420-435`; `is_4bit` fetched, not displayed) | T | 68 |

No paused gate.

### 1.6 CopperPanel — `copper_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 12 | `copper().is_running()` | mode != 0 | T | 101 |
| 13 | `copper().pc()`, `copper().mode()` | 10-bit PC, 2-bit mode | T | 104-105 |
| 14 | `copper().instruction(addr)` ×64 | raw 16-bit words, window centred on PC; decoded WAIT/MOVE/NOP/HALT in the panel (`:73-93`) | T | 114 |

No paused gate.

### 1.7 NextRegPanel — `nextreg_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 15 | `nextreg().peek(i)` ×256 | side-effect-free, un-logged observation (`nextreg.h:62`; NOT `read()`, NOT `cached()` — the comment at `:187-205` is the contract) | T | 206 |
| 16 | `nextreg().write(row, val)` | THE Z80 WRITE PATH with handlers (cell edit) | E | 174 |

No paused gate; signals blocked during refresh (:184).

### 1.8 AudioPanel — `audio_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 17 | `audio_mute_mask()` | seed of the five checkboxes | C | 105 |
| 18 | `set_audio_mute_mask(mask)` | AY0/AY1/AY2/DAC/BEEPER bits | E | 165 |
| 19 | `turbosound().ay(chip).read_register(reg)` 3×16 | AY register file per chip | T | 178 |
| 20 | `turbosound().enabled()`, `.ay_mode()`, `.stereo_mode()` | TurboSound on/off, AY/YM, ABC/ACB — the LIVE signals, deliberately not re-decoded NR bytes (`:191-209`, pinned DAP-02..06) | T | 187-209 |

### 1.9 WatchPanel — `watch_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 21 | `mmu().read(addr[+0..3])` | byte / word / long per watch, CPU address space | T | 69-83 |
| — | `watches_` | **panel-owned** list of {addr, label, type} (`watch_panel.h:46-52`) — not in the core | E | 98-112, 168-183 |

`add_watch(addr,label,type)` is also called by the disassembly context menu (#40-42).

### 1.10 BreakpointPanel — `breakpoint_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 22 | `breakpoints().master_enabled()` | seed + refresh of the master checkbox | C,O | 57, 216 |
| 23 | `breakpoints().set_master_enabled(on)` | GH #225 master switch | E | 64 |
| 24 | `breakpoints().add_observer(fn)` / `remove_observer(id)` | both change kinds | C / dtor | 107, 113 |
| 25 | `breakpoints().pc_breakpoints()` | THE MODEL: addr → enabled | O | 156 |
| 26 | `breakpoints().watchpoints()` | THE MODEL: {addr, type ∈ READ/WRITE/READ_WRITE/IO_READ/IO_WRITE, enabled} | O | 180 |
| 27 | `set_pc_enabled` / `set_watchpoint_enabled` | per-row On checkbox | E | 262-268 |
| 28 | `add_pc` / `add_watchpoint` | Add dialog (6 types) | E | 337-341 |
| 29 | `remove_pc`+`add_pc`+`set_pc_enabled` / watchpoint twin | Edit = remove + add + carry enabled flag | E | 362-378 |
| 30 | `remove_pc` / `remove_watchpoint` | Remove | E | 393-398 |
| 31 | `SymbolTable::lookup(addr)` | Symbol column | O | 240 |

### 1.11 DisasmPanel — `disasm_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 32 | `breakpoints().add_observer` (PcBreakpoints only) / `remove_observer` | gutter redraw | C / dtor | 143-151 |
| 33 | `mmu().read(a)` as `DisasmReadFn` | bytes for `disasm_one` / `instruction_length` / `collect_range` | P,E | 166, 203, 274, 400, 624, 692, 714 |
| 34 | `cpu().get_registers().PC` | current-PC highlight, follow-PC centring, caret fallback | P,E | 206, 266, 311 |
| 35 | `breakpoints().pc_exists(a)`, `.has_pc(a)` | gutter: exists → dot, live → filled (GH #225) | P,O | 207-216 |
| 36 | `breakpoints().pc_exists/remove_pc/add_pc` | gutter click + context "Toggle Breakpoint" | E | 560-566, 812-818 |
| 37 | `emit run_to_requested(addr)` | Enter / context "Run to Here" / bindable Run to Cursor → manager (§1.15 #81) | E | 320, 706, 828 |
| 38 | `cpu().get_registers()` HL DE BC IX IY SP | context-menu register-indirect targets | E | 845-849 |
| 39 | `SymbolTable::lookup` | painter substitution via `disasm_text::apply_symbols` (:530) and menu labels | P,E | 530, 856, 870 |
| 40-42 | `WatchPanel::add_watch(...)` ×3 routes | symbol at line / 16-bit immediate / (rr) | E | 862, 876, 886 |
| 43 | `breakpoints().add_watchpoint(imm or reg, READ/WRITE)` ×4 routes | "Break on Read/Write $imm", "Break on Read/Write (rr)" | E | 908-929 |

Gate: `refresh()` returns while running (:257) — **"disasm does not follow PC while running"**. `activate_follow_pc()` (:263) is called by the manager on every pause edge and by "Go to PC".

### 1.12 MemoryPanel — `memory_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 44 | `mmu().get_effective_page(i)` ×8 | page selector labels "Slot i (page XX)" | T | 198 |
| 45 | `mmu().read(addr)` | CPU view: the address as the CPU sees it; slot view: `(slot<<13)|(addr&0x1FFF)` — i.e. also through the CPU map, NOT the physical page (comment `:130-138` admits the simplification) | **X** | 128, 139 |
| 46 | `mmu().write(addr, val)` | inline hex edit, same two views | E | 148, 153 |
| 47 | `cpu().get_registers().SP` | SP row highlight | X | 305 |

**No paused gate and reads happen in `paintEvent`** (`:286-...`), so this panel reads guest memory whenever Qt repaints, running or not. This is why the `GuestExecutionScope` gate had to be in the core, not around `refresh()` (`inspect_watchpoint_test`, dev-guide "A debugger read is not a guest access").

### 1.13 VideoPanel + VideoLayerView — `video_panel.cpp`

| # | Access | Data | When | Line |
|---|---|---|---|---|
| 48 | `video_timing()` `.hc_max() .vc_max() .max_hblank() .max_vblank() .display_origin() .vblank_top()` | frame diagram geometry + fb-row mapping | T | 997-1001, 1005 |
| 49 | `debug_state().paused()` | raster block shows dashes while running | T | 1003 |
| 50 | `video_panel_raster_state()`: `paused_hc()`, `paused_vc()`, `ula().get_screen_mode_reg()`, `ula().get_shadow_screen_en()` → `raster_state_at(video_timing(), …)` | `RasterState` (raw hc/vc, hc_ula/vc_ula, cvc, phc, region, fetch) — GH #22 | P | 281-285 |
| 51 | `video_panel_layer_state()`: `nextreg().peek(0x15/0x68/0x69/0x6B)` | 4 layer enables + NR 0x15 priority (`peek`, never `cached` — Task 40) | T | 290-298 |
| 52 | `ula().get_active_ula_palette()`, `palette().ula_colour(bank, i)` ×32 | palette swatch | T | 1060-1062 |
| 53 | `mmu().rom_in_sram()` | Layer 2 SRAM bank shift | P | 458 |
| 54 | replay: `palette()/layer2()/sprites()/ula()×3/tilemap()/mmu().attr_mux_*/renderer()` `rewind_to_baseline*` + `apply_changes_for_line*(row)` + `flush_remaining*` | per-scanline change-log walk, the SAME round trip `Renderer::render_frame` does | P | 302-339, 467-471, 597 |
| 55 | `renderer().render_row(dst,row,mmu(),ram(),palette(),layer2(),&sprites(),&tilemap())` | COMPOSITE view = the real compositor row body | P | 497-499 |
| 56 | `ula().render_scanline_bank(dst,row,mmu(),use_bank7, rrrgggbb_to_argb(renderer().fallback_for_line(row)))` + `renderer().apply_ula_clip(dst,row)` | ULA primary / shadow views (bank forced) | P | 514-527 |
| 57 | `layer2().render_scanline_debug(dst,row,ram(),palette(),active_bank()/shadow_bank(),renderer().transparent_rgb_for_line(row),rom_in_sram,ula().get_active_layer2_palette())` | Layer 2 active / shadow views | P | 549-565 |
| 58 | `sprites().render_scanline_debug(dst,row,palette(),ula().get_active_sprite_palette())` | Sprites view | P | 570-572 |
| 59 | `tilemap().render_scanline_debug(dst,ula_over,row,ram(),palette(),nullptr,ula().get_active_tilemap_palette())` | Tilemap view | P | 581-584 |
| 60 | `renderer().fallback_for_line(row)`, `renderer().fallback_colour()` | Background view + its title | P | 596, 603 |
| 61 | `ula().vram_bank7()` | ULA view title "LIVE / NOT live" | P | 614 |

Rows past `vc` are left unrendered; only the VISIBLE tab's view is refreshed (:1067-1073); a tab switch invalidates and refreshes immediately (:953-959) — paused it renders, running it shows the placeholder (until the WP0 fix round a view switched to while running kept the picture of an earlier pause, §6.2a defect 5). The rendered `QImage` is the test seam (`video_panel.h:47`).

### 1.14 DebuggerWindow — `debugger_window.cpp` (menus, toolbar, dialogs)

| # | Access | Data / verb | Line |
|---|---|---|---|
| 62 | `trace_log().enabled()` / `.set_enabled(b)` / `.clear()` / `.export_to_file(path)` | Trace toggle button + Debug ▸ Trace submenu (Enable / Clear / Export…) | 218-238, 547-570, 755 |
| 63 | `rewind_buffer()` `->empty() ->depth() ->oldest_frame_num() ->newest_frame_num() ->snapshot_bytes()` | Frame Back guard, slider range, status text "Rewind: N frames / M MB", "Rewound: frame a of b", size dialog | 259-263, 511-515, 582, 706-718, 777-816, 843-851 |
| 64 | `frame_num()`, `at_restored_frame_start()` | the frame the machine is in, in snapshot-tag numbering (`frame_num()-1`): slider thumb, label, "Rewound" status; Frame Back target = that frame, or the one before when the machine sits on a restored frame start (WP0 fix round, §6.2a defect 1) | `rewind_position()`, `frame_back()` |
| 65 | `resize_rewind_buffer(n)` / `set_rewind_enabled(b)` / `rewind_enabled()` | Enable Rewind toggle (create-or-resume / keep-but-pause) + Rewind Buffer Size… | 593, 596, 608, 828, 888 |
| 66 | `rzx_player().is_playing()`, `rzx_recorder().is_recording()` | `can_rewind` greying (mirrors `Emulator::rzx_blocks_rewind()`) | 713-714 |
| 67 | `debug_state().paused()` | `update_actions(is_paused)` inputs | 225, 551, 613, 784 |
| 68 | `breakpoints().clear_all_pc()` + `.clear_all_watchpoints()` | Breakpoints ▸ Clear All | 660-661 |
| 69 | `breakpoints().add_watchpoint(addr, READ/WRITE/READ_WRITE)` / `.add_pc(addr)` | Breakpoints ▸ Add Read/Write/RW/Execute… (modal hex prompt) | 1205, 1213 |
| 70 | `DebuggerManager::on_rewind_to_frame(prev)` | Frame Back (button + menu) | 263, 515 |

### 1.15 DebuggerManager — `debugger_manager.cpp` (the verbs)

| # | Access | Verb / data | Line |
|---|---|---|---|
| 71 | `debug_state().set_active(false)` | ctor: debugger starts inactive | 35 |
| 72 | `debug_state().set_active(true/false)` + `call_stack().set_enabled(true/false)` | `set_enabled()` = attach/detach | 92-93, 149-150 |
| 73 | `debug_state().paused()` | read at 11 sites (edge detection, guards, `update_actions`) | 110, 124, 335, 383, 417, 454, 477, 522, 669, 698, 728 |
| 74 | `debug_state().resume()` | `on_run()`; disable-auto-resume | 339, 144 |
| 75 | `debug_state().pause()` | `on_pause()`; pre-step pause in step_into/over/out | 357, 384, 393, 418, 455 |
| 76 | `debugger_step()` | `on_step_into()` — GH #207 (frame loop + halt run-out) | 391 |
| 77 | `cpu().get_registers().PC`, `mmu().read` via `DisasmReadFn`, `is_call_like()`, `instruction_length()`, `debug_state().step_over(next_pc)` | `on_step_over()` — the GUI DISASSEMBLES GUEST MEMORY to decide | 421-431 |
| 78 | `cpu().get_registers().SP`, `debug_state().step_out(sp)` | `on_step_out()` | 458-459 |
| 79 | `current_frame_cycle()`, `timing().master_cycles_per_line/_frame`, `video_timing().vblank_top()`, `clock().get()`, `Renderer::FB_HEIGHT`, `debug_state().run_to_cycle(t)` | `on_run_to_eof()` — target = midpoint of the last VISIBLE raw line (`FB_HEIGHT-1+vblank_top`), next frame if already past | 493-504 |
| 80 | same set | `on_run_to_eosl()` — next line start; past the last visible row → next frame start | 526-546 |
| 81 | `debug_state().run_to(addr)` | Run to Here (signal from disasm) | 197 |
| 82 | `rewind_buffer()`, `rzx_blocks_rewind("Step Back")`, `step_back(1)` | `on_step_back()` | 564-568 |
| 83 | `rewind_buffer()`, `rzx_blocks_rewind(...)`, `rewind_to_frame(n)` | `on_rewind_to_frame(n)` | 595-599 |
| 84 | `last_state_error()`, `state_error_generation()` | corruption gate (Task 60e) + warning (60b) | 279-281, 298-303 |
| 85 | `snapshot_raster()` | called BEFORE every paused refresh so `paused_hc/vc` are current | 670 |
| 86 | `SymbolTable::load_z88dk_map` / `load_simple_map` / `size()` | Map menu (file dialogs + message boxes) | 631-634, 649-652 |

### 1.16 Outside `src/debugger/`

| # | Site | Access | Line |
|---|---|---|---|
| 87 | `MainWindow` Debug menu | `config().magic_breakpoint`, `set_magic_breakpoint(checked)` | `main_window.cpp:930, 936` |
| 88 | `MainWindow::keyPressEvent` | forwards Run/StepInto/StepOver/StepOut/Pause from `debug_keys_` to the five manager slots | `:2200-2247` |
| 89 | `MainWindow::closeEvent` | `set_enabled(false, /*prompt_on_corrupt=*/false)` (Task 60f) | `:2700` |
| 90 | `MainWindow::push_debug_keymap` | `DebuggerManager::set_keymap(debug_keys_)` | `:2143` |
| 91 | `QtApp::on_frame_tick` | `check_breakpoint_hit(); refresh_panels();` once per timer tick, AFTER the frame | `qt_app.cpp:666-668` |
| 92 | `Emulator::rewind_to_frame` | re-renders the main framebuffer itself (`emulator.cpp:12869-12871`) — the "framebuffer sync on rewind" is core-side, not GUI-side | |
| 93 | `QtApp` | `on_input_state_restored` re-seeds mouse/gamepad after a restore (Task 60c/79) — not a debugger path | `qt_app.cpp:181-183` |
| 94 | `QtApp::TickEffects::paused()` | `emulator_.debug_state().paused()` — the `FrameSequencer` hook that skips `run_frame()` while the debugger holds the machine (`frame_sequencer.h:209`). Maps onto CAP-CTL-13 `state().paused`; no side channel. (Missed in v1-v3; review N-10.) | `qt_app.h:126` |
| 95 | `emulator_cold_boot()` | saves `debug_state().breakpoints()` (the whole `BreakpointSet`, observers included) and `active()` across the destroy/reconstruct of a load, and restores both afterwards — so a `--load` / File ▸ Load keeps the user's breakpoints and the debugger's attachment. Under the backend this is CAP-CTL-15 `load(path)`'s contract: subscriptions (all owners), the master switch and the attached/live_raster state survive a load (REQ-qt-29). | `src/platform/emulator_boot.h:133-146` |

**Inventory size: 95 numbered rows; 21 core object types.** Three rows are
*edits* of machine state from a panel (#16 NextREG write, #46 memory write,
#18 mute mask — the last is explicitly not machine state, `emulator.h:680-691`);
the control verbs #82/#83 (`step_back`, `rewind_to_frame`) additionally
**rewrite the entire machine** by restoring a snapshot, and #76-#81 execute
guest instructions — the "three" counts panel-originated edits, not every
path that mutates the machine (review N-11).

**Subtleties of the three panel write paths, stated for the backend's write
CAPs to inherit** (owner 2026-09-27: DSL scripts may now mutate the machine
through the same CAPs — `poke`, `set_register`, `port_out`, `nextreg_write`):
* **#16 NextREG write goes through `NextReg::write(reg, val)`, the Z80's
  write path** (`nextreg_panel.cpp:174`): the register's write handler runs
  synchronously, so the side effects a guest `NEXTREG` has (MMU remap on
  0x50-0x57, palette index/value latching on 0x40/0x41/0x44, the clip-window
  4-write rotation on 0x18-0x1C, Layer 2 enable on 0x69, a reset on 0x02, …)
  all happen. It is NOT `cached()`-store-only and NOT a deferred/attributed
  CPU write, and it raises no `NextRegWrite` event (backend CAP-INS-04 says
  `source = Debugger`). Note that `NextReg::write` emits the SAME
  `NextREG write reg= val=` trace line for every caller (`nextreg.cpp:456-457`),
  so a panel edit is indistinguishable from a guest write in that log today;
  the backend's SES-06 `MUTATE <what> <old> -> <new> by <client>` line is the
  record that distinguishes it (review N-10). Backend B2 gates the new
  `NextReg::write` event hook on `guest_access()`, so this panel's own
  `nextreg().write()` (`nextreg_panel.cpp:174`) can never raise a
  `NextRegWrite` on itself. A write
  CAP that skipped the handler would leave the panel's edit invisible to the
  machine, which is the defect the panel would then show (Task 40's class).
* **#46 memory write is `Mmu::write` outside `GuestExecutionScope`**
  (`memory_panel.cpp:143-154`): writes to a ROM-mapped slot are ignored (not
  refused — the panel shows the unchanged byte on the next paint), the
  per-scanline attribute-mux and change logs are updated as for a guest
  write (so a paint-time edit of `0x5800-0x5AFF` while paused shows on the
  next rendered frame), overlays (DivMMC/Multiface/L2 write-over) take the
  write exactly as the CPU's would, and no watchpoint latches and no
  `MemWrite` event is raised (REQ-qt-17b confirmed). After WP8 the slot view
  writes through `MemSpace::Page` instead, which bypasses overlays by
  definition.
* **#18 mute mask is NOT machine state** (`emulator.h:680-691`): it gates the
  output stages only, is not reset, not serialised, not visible to the Z80
  (AY registers still read back). A write CAP for it must keep that contract —
  it must never appear in a snapshot or an RZX.

---

## 2. Control verbs — exact current semantics

All verbs live in `DebuggerManager` (public slots, `debugger_manager.h:66-77`).
Every one starts with `if (!enabled_) return;`. "Gate" = Task 60e
`confirm_resume_if_corrupt()` (`debugger_manager.cpp:297-324`): a modal
Yes/No (default No) when `last_state_error()` is non-empty and this
`state_error_generation()` has not been acknowledged; the pure policy is
`ResumeGuard` (`src/debug/resume_guard.h`).

| Verb | Pre-conditions | What happens | Post-UI |
|---|---|---|---|
| **Run** `on_run` (:326) | paused, else **no-op and the key is still consumed** (GH #223); gate | `DebugState::resume()` — clears step mode, `data_bp_hit`, one-shot; arms the GH #221 step-off | `set_paused(false)` on CPU/disasm/stack/callstack; `resumed()`; `update_actions()` |
| **Pause** `on_pause` (:355) | — | `DebugState::pause()` | `set_paused(true)` ×4; `paused()`; `activate_follow_pc()`; `refresh_panels()` |
| **Step Into** `on_step_into` (:377) | gate; pauses first if running | `Emulator::debugger_step()` — one instruction slot + frame bookkeeping; at HALT runs the halt out (≤2 frames); consumes `data_bp_hit` (`emulator.cpp:10500-10590`) | `pause()`; `set_paused(true)` ×4; `paused()`; follow PC; refresh |
| **Step Over** `on_step_over` (:413) | gate; pauses first | if `is_call_like(PC)` (CALL nn / CALL cc,nn / RST n / DJNZ, `disasm.h:25`) → `step_over(PC+len)` = one-shot at next instruction, resume; **else degrades to Step Into** | resumed-style UI or Step Into's |
| **Step Out** `on_step_out` (:450) | gate; pauses first | `step_out(SP)`; core ends it when a RET-form pops STRICTLY past that SP (GH #203, `debug_state.cpp:74-131`, hook `emulator.cpp:9912-9985`) | resumed-style UI |
| **Run to EOF** `on_run_to_eof` (:475) | paused (else no-op); gate | `run_to_cycle(frame_start + (FB_HEIGHT-1+vblank_top)·line + line/2)`, +1 frame if already past — the last VISIBLE row's midpoint, so the video panel shows a full picture (G164v2) | resumed-style |
| **Run to EOSL** `on_run_to_eosl` (:520) | paused; gate | `run_to_cycle(next line start)`; if that line is past the last visible fb row → next frame start | resumed-style |
| **Run to Here / Cursor** (signal → :194) | gate | `run_to(addr)` = one-shot, `StepMode::NONE` | resumed-style |
| **Step Back** `on_step_back` (:562) | rewind buffer non-empty; not RZX-blocked (refused silently at the GUI; core logs) | `Emulator::step_back(1)`: needs trace enabled + non-empty; target = trace[size-1].cycle; clears trace; `rewind_to_cycle` (restore nearest frame snapshot, replay forward silently); failure → `warn_state_corrupt` ONLY if `last_state_error()` set (benign failures are silent) | `set_paused(true)` ×4; `paused()`; follow PC; refresh |
| **Frame Back** (window `frame_back()`) | buffer non-empty | `on_rewind_to_frame(t)`: t = the frame the machine is in (tag `frame_num()-1`), or t-1 when it already sits on a restored frame start — so each press goes one frame further back (until the WP0 fix round it restored the same frame on every press) | as Step Back |
| **Rewind to frame N** `on_rewind_to_frame` (:593) | as Step Back | `Emulator::rewind_to_frame(n)`: range-checked against buffer; restore that frame's snapshot (frame START), re-render framebuffer, `set_active(true)+pause()` | as Step Back |
| **Rewind slider** (window :305-334) | toolbar visible when `depth()>1` | `sliderReleased` → `on_rewind_to_frame(value)`; Jump Here does the same; thumb follows `frame_num()` only while running (paused: user owns it) | |
| **Enable Rewind** (window :580-614) | — | on: buffer absent → `resize_rewind_buffer(last_rewind_frames_)` (also enables trace); present → `set_rewind_enabled(true)` + `trace_log().set_enabled(true)`; off: `set_rewind_enabled(false)` = keep-but-pause | `update_rewind_ui`, `update_actions` |
| **Rewind Buffer Size…** (:831-890) | — | spin 0..2000 frames; `resize_rewind_buffer(n)` (clears snapshots; 0 = free) | |
| **Trace enable/clear/export** (:214-244, :542-574) | — | `TraceLog` set_enabled / clear / export_to_file(path) with QFileDialog; Step Back's enabled-state gated on `trace_log().enabled()` | `update_trace_indicator` (green/red ball) |
| **Magic breakpoint** (main_window :927-938) | — | `Emulator::set_magic_breakpoint(b)`; core callback does `set_active(true); pause()` (`emulator.cpp:7880-7883`) | |
| **MAP load** `on_load_map_z88dk/simple` (:624-659) | — | `SymbolTable::load_*`; message box with count | panels read the table by pointer |
| **Watches** | — | panel-owned list; Add/Edit/Remove dialogs; three add routes from the disasm context menu | |
| **Data breakpoints R/W/X/IO** | — | `BreakpointSet` mutators from 4 routes: panel Add/Edit/Remove dialog (6 types), Breakpoints menu (Execute/Read/Write/RW), gutter click (Execute toggle), disasm context menu (Read/Write on imm or (rr)); enable flags + master switch (GH #225); every mutator notifies the two views (GH #220) | |
| **Attach/Detach** `set_enabled` (:84-165) | disable while paused → gate (unless `prompt_on_corrupt=false`, app-quit) | `set_active`, `call_stack().set_enabled`, window show/hide + position, `save_position()` on hide | `enabled_changed`; the four `set_paused` seeds |
| **Pause-edge detection** `check_breakpoint_hit` (:682-720) | called every tick | if paused and not enabled → `set_enabled(true)` (GH #219, magic bp); if paused and `!was_paused_` → the pause UI sequence | |

### Panel refresh rules (measured)

* **Paused-only:** CPU, Disassembly, Stack, Call Stack — `set_paused()` flag
  gates `refresh()`; the four calls are repeated verbatim at eleven manager
  sites (:111-118, :199-205, :341-349, :359-367, :395-403, :433-441, :461-469,
  :506-514, :548-556, :575-583, :607-615, :704-711).
* **Every reachable tick (running: ~4 Hz):** MMU, Sprites, Copper, NextREG,
  Audio, Watches, Memory (selector only), Video (layer flags, priority,
  palette; raster block dashes; layer views draw the dim placeholder), Breakpoints.
* **On pause edge only:** `snapshot_raster()` (:670) then a full refresh; the
  disassembly re-centres on PC (`activate_follow_pc`).
* **On observer notification:** Breakpoints list (both kinds), disasm gutter
  (PC kind) — synchronously inside the mutator.
* **On tab switch:** the Video panel's layer sub-view (:953-959).
* **On Qt paint:** the Memory panel reads memory in `paintEvent`.

---

## 3. Projection onto the backend

Mapped onto `backend.md` **v1** and re-verified against **v3** (`doc/design/debug-subsystem/backend.md`,
§4.1-4.8). `CAP` = the id that serves the row; `+REQ` = a capability the row
needs that v1 does not carry (sent to `design-backend`, ledger §8). Each REQ
is sent to `design-backend` verbatim; answers are recorded in §8.

### 3.1 The rule this projection follows

The Qt adapter may call **only** the backend surface plus Qt. Anything the
GUI computes from raw machine state today that another frontend would have
to recompute identically is a backend responsibility. Five such computations
exist and are the real content of #278 (everything else is renaming an
accessor):

1. **Step Over's call-like decision** (#77) — the GUI disassembles guest
   memory to decide. DZRP declines this (DeZog computes lengths client-side,
   gh12 comment 2026-08-03), but ZRCP, GDB and the DSL all need `step_over`
   to mean the same thing the GUI means. → backend verb.
2. **Run-to-EOF / EOSL target arithmetic** (#79, #80) — raw-VC/`vblank_top`
   arithmetic that was wrong once already (G164v2 comment at :486-492).
   → backend verbs `run_to_end_of_frame()` / `run_to_end_of_scanline()`.
3. **`snapshot_raster()` before a paused read** (#85) — the GUI has to
   remember to call it. → the backend's pause path does it; raster state is
   always current when `paused()` is true.
4. **The pause-edge detection** (`was_paused_`, #73) — polling a bool per
   tick. → the backend reports pause transitions (an epoch counter and/or an
   observer; §4).
5. **The per-layer render pipeline** (#53-#61, 240 lines) — `render_to_image`
   is a pure function of `Emulator&` + layer + `vc` producing 640×256 ARGB
   rows, wrapped in `QImage`. It is the most expensive inspection the GUI
   does and the only one that touches nine subsystems. → backend
   `render_layer(layer, vc, dst, stride)`; the Qt view keeps only the
   `QImage` wrapper, the checkerboard and the title strings.

### 3.2 Projection table

`Row` = §1 numbers. `Need` = the backend capability in the GUI's words.

| Rows | Need | CAP (backend.md v1) | +REQ / notes |
|---|---|---|---|
| 71-72, 89 | attach/detach; the "active" render hint; call-stack tracking on while attached | CAP-SES-01, CAP-SES-05, CAP-INS-12 (`set_enabled`) | **REQ-qt-01b**: per-client `set_live_raster(cid, bool)` — the Qt adapter stays attached for the process lifetime (§4) and toggles this when the window shows/hides (**as built, WP2: attached only while the window is open**, because an attach arms the machine — §4.1; REQ-qt-32 is the owner-approved way back to a lifetime listener); **REQ-qt-01c (finding)**: `DebugState::active()` also gates the STEP machinery (`OUT`, `STEP_BACK`, `RUN_BACK_TO_CYCLE` — `debug_state.h:17`, `emulator.cpp:9251,9916`), so CAP-SES-05 must not tie those to `live_raster` |
| 73, 67, 49 | `paused()` | CAP-CTL-13 `state().paused` | — |
| 74 | resume with GH #221 step-off and GH #223 no-op | CAP-CTL-02 | — |
| 75 | pause | CAP-CTL-01 | — |
| 76 | step into = `debugger_step()` | CAP-CTL-03 | — |
| 77 | step over decided in the backend | CAP-CTL-04 | — |
| 78 | step out | CAP-CTL-05 | — |
| 81, 37 | run to addr | CAP-CTL-06 | — |
| 79, 80 | run to EOF / EOSL | CAP-CTL-08 | — |
| 82, 83, 70 | step back / rewind to frame with a three-way outcome | CAP-CTL-09, CAP-CTL-10 (`Result`) | **REQ-qt-08b**: `Result` lacks a BENIGN refusal for "buffer empty / trace disabled / frame out of range" (today silent, `debugger_manager.cpp:564,595` + `warn_state_corrupt` keyed on `last_state_error()`); add `RefusedUnavailable` (or equivalent) so the adapter never shows the corruption modal for a benign failure |
| 63-65 | rewind buffer enable (keep-but-pause) / resize / range | CAP-ST-03 | **REQ-qt-09b**: `snapshot_bytes()` — status bar "N frames / M MB" and the size dialog's estimate (`debugger_window.cpp:816, 850`) |
| 64 | frame number, cycle | CAP-INS-07 `time()` | — |
| 66 | RZX-blocks-rewind BEFORE the verb (greying) | — | **REQ-qt-09c**: `rewind_blocked() -> bool` (or a `blocked` flag in `rewind_range()`) — `update_actions` needs it before the click (`:709-714`); `Result::RefusedRzx` is after the fact |
| 62 | trace enable / clear / export | CAP-INS-13 | **REQ-qt-10b**: `trace_enabled()` query (`:218, 755`) and `trace_clear()` (`:558`) are not listed |
| 84 | corruption observables | CAP-CTL-11 (`CorruptionIncident{subsystem, generation}`, `acknowledge_corruption`) | — (`warn_state_corrupt`'s subsystem string comes from the incident) |
| 87 | magic breakpoint get/set | — | **REQ-qt-12**: v1 has the `Magic` event kind and pause reason but no `magic_breakpoint()` / `set_magic_breakpoint(b)` (MBP-01/02 pin the menu toggle, `main_window.cpp:930-936`) |
| 22-30, 32, 35, 36, 43, 68, 69 | the breakpoint model + mutators + observer | CAP-EVT (`subscribe/unsubscribe/set_enabled/master`), CAP-INS-17 `breakpoints()`, CAP-SES-02 `SubscriptionsChanged` | **REQ-qt-13b**: `SubscriptionsChanged` must carry the kind (PC-execute vs data) — the gutter re-disassembles on PC changes only (GH #220, `disasm_panel.cpp:143-147`); **REQ-qt-13c**: READ_WRITE is ONE row today (`WatchType::READ_WRITE`, listed/edited/removed as one) — either the kind filter is a bitmask (`MemRead\|MemWrite`) or the adapter must pair two subscriptions and hide the seam; **REQ-qt-13d**: the GUI-created subscription has `owner = this client`, no condition, `once = false`, `action = Stop`; the panel lists every owner's rows (design-gdb REQ-gdb-12 tagging) |
| 10, 31, 39, 86 | symbol table | CAP-SYM | — |
| 73 (edge), 91 | pause/resume transitions | CAP-SES-02 (`Paused{by, reason, cycle, pc}`, `Resumed{by}`), CAP-CTL-13 | **REQ-qt-15b**: the adapter's listener only RECORDS the transition; UI work runs at the next tick (§4). No epoch counter needed if the listener is reliable; keep `state().paused` as the pull fallback |
| 1, 7, 34, 38, 47 | registers | CAP-INS-01 (`registers()`; `set_register` declined) | — |
| 8, 21, 33, 45 | memory read, non-perturbing (incl. F1: no +3 floating-bus latch) | CAP-INS-02 `peek(MemSpace::Cpu, …)`; **the Memory panel's slot view (owner Q7, WP8): branches on CAP-INS-03 `SlotInfo` — a RAM slot reads `peek(MemSpace::Page{nr_page}, …)` (NR 0x50-0x57 space, the backend applies `to_sram_page`); a ROM slot reads `peek(MemSpace::Rom{…}, …)`, because `Mmu::get_effective_page()` returns an SRAM-PHYSICAL index for a ROM slot (`mmu.h:74-77`; `map_rom_physical` stores `rom_page` without `to_sram_page`, `mmu.cpp:396-402`) that is NOT in `Page{}`'s number space (review R-3). REQ-qt-31 asks the backend to publish the space in `SlotInfo` so the adapter never composes it** | — |
| 46 | memory write: CPU view through the CPU map; slot view through the slot's physical space (owner Q7, WP8) | CAP-INS-02 `poke(MemSpace::Cpu, …)` / `poke(MemSpace::Page, …)` for RAM slots; a ROM slot's `poke` is `RefusedReadOnly`, rendered "unchanged" | **REQ-qt-17b (confirm)**: `poke(Cpu)` = `Mmu::write` semantics (ROM ignored, per-scanline logs and attribute mux updated, no watchpoint) — the memory panel's identity depends on it |
| 4-6, 44, 2 | MMU view | CAP-INS-03 (`SlotInfo{page,is_rom,effective}`, `port_7ffd`) | `rom_in_sram` no longer needed by the GUI once CAP-INS-14 owns the render |
| 15, 51 | NextREG peek | CAP-INS-04 | — |
| 16 | NextREG write (handlers run, synchronous) | CAP-INS-04 `nextreg_write` | — |
| 11 | sprite attributes ×128 | CAP-INS-08 (`SpriteInfo` span; pattern RAM / palette / clip declined) | — |
| 12-14 | copper view | CAP-INS-09 | — |
| 19, 20 | AY regs; TurboSound enabled / AY-vs-YM / ABC-vs-ACB | CAP-INS-10 (`ay_registers`) | **REQ-qt-22b**: `turbosound_enabled()`, `ay_mode()`, `stereo_mode()` — the LIVE signals (`turbosound.h:42,51,61`), pinned by DAP-02..06 against re-decoding NR bytes |
| 17, 18 | mute mask | CAP-INS-10 | — |
| 9 | call stack | CAP-INS-12 | — |
| 48, 50, 85 | raster state (snapshotted by the backend on pause) + frame-diagram geometry | CAP-INS-06, CAP-TIME-01 `machine_timing()` | **REQ-qt-25b**: `machine_timing()` lists cycles/line/frame, lines, divisor; the diagram needs `hc_max, vc_max, max_hblank, max_vblank, display_origin{hc,vc}, vblank_top` (`video_panel.cpp:997-1001`, DVP-RAS-*) — add them (or a `raster_geometry()`) |
| 3 | ULA mode bits + shadow bit (CPU panel label) | CAP-INS-15 ("ULA screen regs") | — |
| 51 | layer enables + priority | derived in the adapter from CAP-INS-04 ×4 (`video_panel_layer_state`, DVP-LS/PEEK) | no REQ — presentation, 4 peeks |
| 52 | ULA palette, active bank, ARGB | CAP-INS-15 `palette(PaletteId) -> span<uint16_t>` | **REQ-qt-27b**: which bank is ACTIVE (`ula().get_active_ula_palette()`, NR 0x43) and the RGB333→ARGB conversion (`Renderer::rrrgggbb_to_argb` / `PaletteManager::ula_colour`) — either a `PaletteId::UlaActive` or an `active_bank()` query; the adapter can convert 9-bit→ARGB if the backend publishes the one function |
| 53-61 | per-layer render | CAP-INS-14 `render_layer(Layer, out)` | **REQ-qt-28**: the split proposal is §3.7; titles need `ula_live_bank7()` (CAP-INS-15 "active screen") and NR 0x4A (CAP-INS-04 peek) |
| 33, 39 | disassembly | CAP-INS-11 or `src/debug/disasm.*` over `peek(Cpu)` (both published) | — |
| 21, 40-42 | Watches (byte/word/long display) | **stay GUI-owned**; values via CAP-INS-02 | agreed with design-dsl: a watch is a peek, not an event (§3.5) |
| 88, 90 | keymap model | stays `src/debug/debug_keymap.*` | §5 |

### 3.3 Backend capabilities the GUI will NOT use ("unused by Qt")

Declined by the Qt adapter for #278 (no feature is added; the GUI is a
projection of exactly what it does today):

| CAP | Why unused |
|---|---|
| CAP-CTL-07 `run_to_cycle` | internal to CAP-CTL-08; the GUI has no "run to cycle" control |
| CAP-CTL-12 `reset` | the Machine menu resets through `MainWindow`, not the debugger; unchanged by #278 |
| CAP-INS-01 `set_register` | no register editing in the GUI |
| CAP-INS-02 `MemSpace::Rom` | **no longer declined (review R-3):** the slot view's ROM slots must read `Rom{…}`, not `Page{}` — see §3.2 row 8. WP8 reads `SlotInfo.space` (REQ-qt-31 accepted; backend §11 item 1 closed) |
| CAP-INS-03 `set_mmu_slot` | no MMU editing in the GUI |
| CAP-INS-05 `port_in/out` | no port I/O in the GUI |
| CAP-INS-08 pattern RAM / sprite palette / clip | the Sprites panel shows attributes only |
| CAP-INS-13 `trace_entries()` | the GUI exports to a file; it never lists entries |
| CAP-INS-14 `framebuffer()` | the emulator window reads it directly; not a debugger-panel path |
| CAP-INS-16 `input_state()` | recorder-only |
| CAP-EVT conditions, ranges (`lo != hi`), `once`, `Log`/`Continue`, `NextRegWrite`, `Frame`, `Scanline`, `Cycle`, `Reset`, `Host`, physical-page filter | the GUI creates single-address `Execute` / `MemRead` / `MemWrite` / `PortRead` / `PortWrite` subscriptions with `Stop` and no condition — nothing else exists in its dialogs |
| CAP-TIME-02/03 | no GUI control for them |
| CAP-IN-01..04, CAP-CAP-01..03 | input injection / capture are CLI and script paths; the main window's own screenshot is not a debugger feature. **Consequence (review N-9):** `QtApp`'s private frame countdowns (`qt_app.h:184-203`: inject, load, screenshot, exit) are NOT touched by #278; the architecture's §8 claim that "the second copy of the countdowns goes away" has no owner in this document — it belongs to the backend's own work package for CAP-IN/CAP-CAP, and arch §8 now assigns it to the backend's CAP-IN/CAP-CAP package (B4), not #278. **Settled in B4 (owner decision 2026-09-28, O2): the countdowns STAY** — they count loop ticks, which survive a cold boot and advance while paused, and `Frame` subscriptions could not; only the screenshot ACTION moved to the backend (`screenshot()` queued at the count's zero, written by the `post_frames` pump, its outcome from `flush_captures()`). `QtApp` hosts the one `Debugger` (`QtApp::debugger()`), which Q's `DebuggerManager` is to take rather than construct its own |
| CAP-ST-01/02 bookmarks | no GUI control; Save Snapshot is the JNS path (#27), unchanged |
| CAP-SES-03 `pump`, CAP-SES-04 stop policy | **used by `QtApp`** (the loop owner), not by the panels: `pump(0)` per tick next to today's `check_breakpoint_hit()` call (`qt_app.cpp:666`), policy `Pause` |

### 3.4 Reach-arounds — count against v1

Of the 95 inventory rows, **93 map onto a v1/v3 CAP** (#94 → CAP-CTL-13; #95 → CAP-CTL-15 with REQ-qt-29) (with the 14 sub-REQs of
§8, all answered, making the mapping exact) and **2 stay GUI-side by design**
(Watches display list; layer-state derivation from four peeks).
**Reach-arounds remaining = 0; REQs open = 0** (REQ-qt-29 accepted) — at design time; the WP0 fix round opened REQ-qt-09d (§8). The implementation gate is
`grep -l 'core/emulator.h' src/debugger/*.cpp` empty (WP7).

### 3.5 Watches and data breakpoints vs the DSL (design-dsl's point 1)

`design-dsl` asks that a GUI data breakpoint be the degenerate case of the
backend's range/predicate subscription (`REQ-dsl-1/9`). Agreed, with the
four properties the GUI cannot give up, all pinned by suites today:

1. **The model is listable**: the Breakpoints panel shows every entry, with
   its own `enabled` flag, and the master switch round-trip is exact
   (GH #225, `bp_enable_test` BPEN-*, `menu_test` BPEP-*). A subscription
   the panel cannot list would be the first invisible breakpoint.
2. **Mutators notify, with a kind** (GH #220, `menu_test` GH220-*): the
   gutter re-disassembles on PC-kind changes only.
3. **Hot-path cost is unchanged** for the single-address case: the live cache
   + `has_any_watchpoints()` pre-gate (`breakpoints.h:175-182`, dev-guide
   "Enabling and disabling breakpoints"). A range/predicate design must not
   make a one-address write watch dearer than it is now.
4. **The IO port-masking rule** (GH #222, `io_watchpoint_test`): `addr < 0x100`
   = low-byte match, else exact.

Proposal to `design-backend` (REQ-qt-13b): ONE model, entry =
`{kind ∈ pc|mem|io|nextreg, lo, hi, access mask, enabled, owner ∈ user|script, optional predicate/action}`.
The GUI creates `lo == hi`, no predicate, owner=user; it LISTS every entry
(script-owned rows read-only, marked in the Type column — a presentation
change for the DSL PR, not for #278). GUI **watches** (byte/word/long display)
are NOT events and stay in the Qt panel; if the DSL wants a "peek expression"
it is a read, not a subscription.

### 3.7 The `render_layer` split (CAP-INS-14, requested by design-backend)

Today (`video_panel.cpp`): `VideoLayerView` (widget) owns a `QImage`
(`:352, 462`), `render_to_image(vc)` (`:394-630`) does the whole render, and
three static helpers (`replay_rewind/line/restore`, `:302-339`) do the
per-scanline change-log walk. Proposed cut, measured against what each line
touches:

**Backend** (`jnext_debug`, Qt-free), `render_layer(Layer layer, int vc, uint32_t* dst, size_t stride_px)`:

* `Layer` = the eight-value enum at `video_panel.h:20-29` (COMPOSITE,
  ULA_PRIMARY, ULA_SHADOW, LAYER2_ACTIVE, LAYER2_SHADOW, SPRITES, TILEMAP,
  BACKGROUND), moved to `inspect.h`.
* Width is always 640 (`Renderer::FB_WIDTH`, the G104 canonical width —
  every branch of the switch at `:409-455` resolves to 640); height 256.
* Contract: for rows `0..vc` the backend first fills the row with
  `0x00000000` and then renders; a cell the layer does not paint, or that
  `apply_ula_clip` zeroes, stays alpha 0 = **transparent**. Rows `> vc` are
  **not touched**. COMPOSITE writes every cell (the compositor emits the
  fallback colour where all layers are transparent, `:483-499`).
* Everything between `replay_rewind(emu)` (`:471`) and `replay_restore(emu)`
  (`:597`) moves verbatim, plus `rom_in_sram` (`:458`). The state-preserving
  round trip (`:225-241`) is the backend's guarantee.
* `vc < 0` (running) is not a backend call: the widget draws the placeholder.

**Widget** (`VideoLayerView`, Qt): the `QImage` (allocate 640×256 once),
the "unrendered" fill for rows `> vc` and the running placeholder
(`UNRENDERED_ARGB`), the checkerboard composed under every alpha-0 cell of
rows `≤ vc` (today's `fill_checker` + `restore_checker_where_transparent`,
`:195-215`, applied uniformly instead of per-layer), the DPR scaling and the
red raster line (`:632-700`), and the two title strings (`:603, :610-622`)
from CAP-INS-04 `peek(0x4A)` and CAP-INS-15's active screen.

**Risk, and why it is NEEDS-PROTOTYPE**: the checkerboard is applied
per-layer today (ULA views get it AFTER the clip, others BEFORE the render).
The uniform "0 = transparent" contract is equivalent only if no layer emits an
opaque pixel with alpha 0 — `Renderer::rrrgggbb_to_argb` and the palette ARGB
cache set alpha FF, but this must be proved by running the 106 DVP rows
against the moved function before the widget is touched (WP4d). If any row
moves, the contract is wrong and the per-layer checker stays in the widget.

### 3.6 Symbol table ownership

Today the Qt manager owns it (`debugger_manager.h:131`). Every other frontend
needs the same table (DSL `on execute <symbol>`, DZRP labels are client-side
but ZRCP/GDB want names). → backend session owns it (REQ-qt-14); the Qt
panels take a `const SymbolTable*` from the backend exactly as they take one
from the manager today, so `set_symbol_table()` call sites do not change.

---

## 4. Refresh model

**Keep pull, keep single-threaded, add a transition counter.**

What the panels need, measured: (a) a full refresh at every pause edge, with
the raster snapshot taken first; (b) a throttled refresh of the "running"
panels; (c) instant redraw of the two breakpoint views on mutation; (d) a
Video re-render only for the visible tab. None of that wants a push model —
a paused machine does not change, and while running the panels deliberately
show a 4 Hz blur or nothing.

What a snapshot/"debug view" object would cost: the Memory panel reads up to
64 KB lazily in `paintEvent`; the Video panel's render needs nine live
subsystems and the per-scanline change logs (a snapshot could not carry the
replay); the NextREG panel needs `peek` handlers that compose values from live
subsystems. A snapshot big enough to serve the GUI is the rewind snapshot
(~2 MB), taken at 4 Hz for nothing. **Read-through accessors, not a snapshot.**
The backend guarantees each accessor is non-perturbing (the existing
`GuestExecutionScope` rule makes that true by construction for memory;
`peek` for NextREG; `render_layer` is state-preserving by the same round trip
the compositor performs, `video_panel.cpp:225-241`).

Transitions: the backend exposes `pause_epoch()` (incremented on every
running→paused edge, whoever caused it — breakpoint, watchpoint, magic bp,
step completion, rewind, script `stop`) and `pause_reason()`. The Qt adapter
compares the epoch once per tick where `check_breakpoint_hit()` runs today
(`qt_app.cpp:666`): this replaces `was_paused_` with no behaviour change and
no callback re-entrancy. The backend may ALSO offer synchronous observers
(`on_paused`) for the DSL, which must act AT the event; **the Qt adapter must
not do UI work inside such a callback** — it fires from inside `run_frame()`
/ `debugger_step()`, and `set_enabled(true)` does `show()/raise()/
activateWindow()` (`debugger_manager.cpp:97-99`). If the adapter subscribes
at all it only records the epoch.

Threading: unchanged. Qt owns the loop (`qt_app.cpp:203`), the sequencer
skips `run_frame()` while paused (`frame_sequencer.h:209`), every verb runs on
the Qt thread between frames. A remote frontend that needs a thread is the
backend's problem to solve without changing this (that is `design-dzrp`'s
brief, not this file's).

### 4.1 As built (WP2, 2026-09-29) — two corrections to the text above

**1. There is no `pause_epoch()`.** The frozen `debugger.h` never grew one, and
none is needed. The published routes are SES-02's listener (`Paused` /
`Resumed`, pushed only from `pump()`) and CTL-13's `state()`. WP2 uses
`state()`, PULLED once per tick in `check_breakpoint_hit()` — which QtApp calls
after its pump, so any other client's transition inside that pump is already
visible. The adapter compares `state().paused` with what the window last SHOWED
(`DebuggerManager::shown_paused_`, the window's own presentation state — not a
copy of the machine's) and applies the machine's state when they differ.
The listener was weighed and adds nothing for this adapter: its own verbs apply
their UI synchronously (QSI-04, QPE-05 pin "at once, no tick"), so a listener
design would still need the window's shown state to tell "the push for the
pause I already showed" from a new one — and with that state in hand, the pull
is exact in every reachable case. It also keeps the Qt fixtures free of a pump.
The one case a pull cannot see — a stop and a resume both inside one pump,
with no frame between them — ends running, and showing a pause that is already
over is not wanted either. `resume_generation()` stays internal.

The pull also shows a RESUME the window did not cause (another client's
`run()`): the window used to learn of resumes only from its own verbs, and a
remote resume would have left it in the paused shape — frozen panels on a
running machine, deaf to its next pause edge (QPE-07; QPE-08 is the pause twin).

**2. The adapter is attached while the window is OPEN, not for the process
lifetime** (manager decision, 2026-09-29). A client attach ARMS the machine:
`armed = active || clients_attached || persistent` (`debug_state.h`, §5's
`attached || persistent`). A lifetime attach therefore keeps the legacy PC
breakpoints (`should_break()`) and the watchpoints (`wp_live = armed &&
guest_access`) live with the window shut — `--persistent-breakpoints` (GH #219)
turned on by default. `debugger_persistent_bp_test` PBPUI-03 pins the opposite
("default: a closed window disarms, the machine runs past the breakpoint";
it asserts `!armed()`), so a lifetime attach fails an identity row: a
behavioural finding, not only a cost one. It is also a cost: with the window
closed a lifetime attach runs the GUI armed, +1.3-3.0 % instructions (§4.2).
So `set_enabled(true)` attaches (`attach_backend()`) and requests the live
raster; `set_enabled(false)` resumes a paused machine as it always has
(whoever paused it) and detaches (`detach_backend()`). The policy is those two
functions and nothing else, so a later decision to flip it is one change.
Nothing the lifetime attach was for is lost: a magic, persistent or remote
pause opens a closed window because the pause is pulled, not pushed
(PBPUI-06..08; GH #219 and owner Q5 unchanged).

**The general form of the finding: "attach" conflates LISTEN with ARM.** SES-01
has one way to become a client, and it arms the machine. A frontend that only
wants to HEAR the session — a window that must learn of a pause while it is
closed, a loop owner that must learn of an `ExitRequested` — cannot do so
without also switching the hot path on, which is the same class as B4's O5 (a
loop owner has no non-arming way to hear `ExitRequested`). A non-arming
observer attach (a `ClientInfo` flag, or an `observe()` verb, that installs a
listener and counts toward no arm bit) would let a lifetime attach coexist with
PBPUI-03. That is a change to the frozen `debugger.h`: it is REQ-qt-32 (§8),
OWNER-APPROVED 2026-09-29, and it lands when package S or a remote server first
needs it — not in Q.

**Transitional:** the window no longer SETS the legacy `DebugState::active()`
bit — the attach and the live raster replaced it. WP3 retired its two rewind
writers (B3 obligation 3: the replay is armed by `DebugState::ReplayArmScope`
for its own loop only). Its one remaining writer is the magic-breakpoint hook,
which must hold a stop with no client attached at all (headless, SDL, a closed
window). Closing the window therefore still CLEARS it (`set_enabled(false)`), as
it did before, or one magic hit would leave the machine armed with the window
shut (PBPUI-09). The bit, the magic hook's write, the window's clear and
`emulator_cold_boot()`'s copy of it retire together with B3 obligation 1, in
WP4c (§7).

**Closing the window while a magic breakpoint holds the machine — SETTLED by
the owner, 2026-09-29 ("ctl-13: agree").** Architecture §4.1 CTL-13 (owner
decision 2026-09-27) said closing the Qt debugger window must NOT resume a
`Magic` pause. The window has always resumed ANY pause on close, and applied
literally the CTL-13 text would leave a magic-paused machine paused with the
window shut — which the next tick's GH #219 auto-open reopens at once, so the
window could not be closed at all. The review of this milestone put the
conflict to the owner, whose ruling is: **closing the window resumes, whoever
paused, `Magic` included, as it always has; the design text is amended.** The
no-owner rule for `Magic` / `Corrupt` binds SES-01's `detach()` (a client's
departure never releases an unowned pause) and nothing else; the Qt window's
close is a user's Run, not a detach. CTL-13 is amended accordingly (arch §4.1).
No row pins a close while magic-paused (PBPUI-09 resumes first); the behaviour
is `set_enabled(false)`'s, unchanged by WP2.

### 4.2 The hot-path cost of an attached client (interleaved A/B, WP2)

What a process-lifetime attach would cost the GUI with its window CLOSED:
- U is this tree with nothing armed (the attach-on-enable GUI, window closed —
  what ships).
- C is one client attached with nothing subscribed (`JNEXT_BENCH_WATCH=c`) —
  what a lifetime attach would run.
- P is `--persistent-breakpoints` alone, for comparison.

B is `main` @ `85135a744`. Every run is headless `--benchmark`, interleaved,
Release.

| Workload | instructions:u, C vs U | wall T/s, C vs U (spread U / C) | instructions:u, P vs U |
|---|---|---|---|
| boot-48k (600 frames) | **+1.29 %** | −1.6 % (4.4 / 3.3 %) | +1.23 % |
| boot-nextzxos (400) | **+2.98 %** | −3.9 % (5.3 / 5.3 %) | +2.18 % |
| beast.nex (400) | **+2.49 %** | −1.4 % (4.9 / 4.9 %) | void (the load re-latches the flag) |
| copper_demo.nex (400) | **+2.72 %** | void (spread 8-11 %) | void |

Source files:
- instructions: `test/bench/perf-attach.sh` →
  `test/bench/attach-perf-aa02eaf51.txt`, three interleaved rounds;
  deterministic to ~0.01 % run to run;
- wall clock: `AB_SET=attach test/bench/ab-hotlatch.sh` →
  `test/bench/attach-aa02eaf51.txt`, five pairs, host load 3.7 → 2.5. An earlier
  run at load 8-15 was void and is not committed.

B ≡ U to within 0.00-0.02 % in instructions, and within noise in T/s: with
the window closed, WP2 costs nothing.

**A lifetime attach would cost 1.3-3.0 % more work on every instruction the
guest runs, permanently, and 1.4-3.9 % in wall-clock throughput where the
measurement holds.** The costs, per emulated instruction:
- the armed block (`paused()`, the step-off consume, `should_break()` — an
  out-of-line `unordered_set` lookup — the `Execute` gate, the step-mode tests);
- the post-instruction `events_pending()` / `data_bp_hit()` tests;
- the second stage of the eight MMU watch sites;
- on top of P's cost, the `attached()` Step Out test.

That is the behavioural finding's price (PBPUI-03 above) and the argument for
REQ-qt-32.

---

## 5. Where the Qt headers move; what `DebuggerManager` becomes

### 5.1 The two headers

`src/debug/debug_keymap_qt.h` and `src/debug/menu_bar_alt_nav_qt.h` are
header-only and are included by BOTH `jnext_gui` (`main_window.cpp:22,26`,
`host_chords.cpp:3`, `shortcut_capture_button.cpp:3`) and `jnext_debugger`
(`debugger_window.cpp:2-3`), plus `test/debugger/keymap_test.cpp:68`. Neither
library may depend on the other (`CMakeLists.txt:331-337`: `ENABLE_QT_UI=OFF`
+ `ENABLE_DEBUGGER=ON` builds `jnext_debugger` with no `jnext_gui`), which is
the documented reason they sit in `src/debug/` (`debug_keymap_qt.h:5-10`).

Move both to a new header-only directory **`src/qt/`** (no library target;
`target_include_directories(... ${CMAKE_SOURCE_DIR}/src)` already makes
`#include "qt/debug_keymap_qt.h"` resolve for every Qt target). Rules for the
directory: headers only, Qt-only, no `Emulator`. Five includes and one test
include change; `jnext_debug` stays Qt-free by directory and by grep
(`grep -l '<Q' src/debug/*` empty — a lint row for the test plan, §6).

### 5.2 `DebuggerManager` → a thin Qt adapter

Keep the class name and its public slots. The suites drive it by name
(`debugger_persistent_bp_test` PBPUI-*, `debugger_quit_gate_test` QG-*,
`debugger_video_panel_test` DVP-11, `debugger_menu_test` GH223-*,
`debugger_keymap_test` DKM-*), and #278 is judged on behaviour identity: a
rename buys nothing and costs every fixture. What changes is inside:

| Today | After |
|---|---|
| ctor takes `Emulator*` (`:23`) | ctor takes the backend session (`DebugSession&` or whatever `backend.md` names it) — **as built (WP2):** `DebuggerManager(QMainWindow*, jnext::dbg::Debugger&, Emulator*, QObject*)`: QtApp's one backend, handed over by `MainWindow::set_debugger()`; the `Emulator*` stays for the panels (WP4/WP7), the rewind verbs (WP3), the raster snapshot (WP4d) and the legacy `active()` clear (§4.1) |
| `emulator()` accessor (`:64`) | removed — no caller in `src/` (measured: `grep -rn '->emulator()' src/` finds none from the debugger) |
| eleven copies of the four `set_paused()` calls | one `apply_pause_state(bool)` driven by the epoch — **as built:** driven by the tick's `state()` pull (§4.1); the enable seed keeps its own four-panel call (`set_panels_paused()`), because it deliberately does not count as the pause edge |
| `on_step_over` disassembles | `session.step_over()` |
| `on_run_to_eof/eosl` compute cycles | `session.run_to_end_of_frame()/…scanline()` |
| `refresh_panels` calls `snapshot_raster()` | nothing — backend keeps it current |
| `check_breakpoint_hit` compares `paused()` with `was_paused_` | compares `pause_epoch()` with the last seen epoch; the GH #219 auto-enable branch is unchanged — **as built:** compares `state().paused` with the window's shown state, both directions (§4.1) |
| owns `SymbolTable` | takes it from the session; `on_load_map_*` keep the QFileDialog/QMessageBox and call `session.symbols().load_*` |
| `ResumeGuard` + modal | unchanged (the modal is Qt; the policy is already pure) — reads `session.state_error()` / `state_error_generation()` — **as built:** `resume_blocked_by_corruption()` / `acknowledge_corruption()` (CTL-11); the manager's own `ResumeGuard` is gone, so the modal and the backend's verbs gate on one acknowledgment; the app-quit path acknowledges without asking (QG-06) |
| `resumed()` / `paused()` / `enabled_changed()` Qt signals | unchanged; emitted from the adapter, never from a backend callback |

`DebuggerWindow` and the 13 panels take the same session reference instead of
`Emulator*`; each panel's `refresh()` body is a one-for-one accessor rename
(§3.2). `VideoLayerView::render_to_image` shrinks to: allocate `QImage` of
the right width, `session.render_layer(layer, vc, scanLine(0), stride)`,
checkerboard where alpha == 0, titles from `ula_live_bank7()` /
`fallback_colour()`.

The SDL frontend is untouched by #278 (it has no debugger UI; `src/platform/`
touches no `debug_state`, measured).

---

### 5.3 Script host keys Alt+1..8 — how the Qt window delivers them (review R-6)

Owned by the DSL PR (arch §10.1, S-WP5 "GUI (Script tab, Alt+1..8)", which
depends on Q), not by #278; this section fixes HOW the Qt side delivers them.
**Agreed with design-dsl 2026-09-26: `dsl-frontend.md` §6.4 mirrors this
section verbatim in substance (routing, swallow, QActions, refusal, SDL twin,
row names), and its §6.6 man-page text states the user-visible change.** Verified against the code:

* **Today Alt+<digit> reaches the guest as the bare digit.** `Keyboard::set_key`
  uses an Alt variant only when `s_alt_compound[sc]` / `s_alt_extkey[sc]` has
  an entry (`keyboard.cpp:361-363`); digits have none, so `Alt+1` types `1`.
  Claiming Alt+1..8 therefore **removes eight guest keystrokes** — a
  user-visible change that is stated here and pinned (rows below), not
  assumed away by "no host-chord collision" (which is true: the TASK-115
  inventory and `main_window.cpp`'s `Qt::ALT` shortcuts are letters).
* **Routing must not depend on the debugger being open.** The keymap
  forwarding block is guarded by `if (debugger_mgr_ && debugger_mgr_->is_enabled())`
  (`main_window.cpp:2200`); ChaseTheBug's use case is arming/disarming after
  boot with no debugger window. So: a separate block in
  `MainWindow::keyPressEvent`, evaluated BEFORE that guard and independent of
  it, matches `Alt` + `Key_1..Key_8` with modifiers exact, calls the adapter's
  `raise_host_event("scriptN")` (CAP-EVT `Host`), accepts the event and
  returns — the key never reaches `handle_key()` and never reaches the matrix.
  The key-UP is swallowed the same way (the Alt-compound release rule,
  `main_window.cpp:2385-2420`, already shows the shape).
* **In the debugger window** (when it has focus): eight `QAction`s with
  `Qt::WindowShortcut` context on `DebuggerWindow`, not menu items (the
  `run_to_cursor_action_` pattern, `debugger_window.cpp:533-537`), calling the
  same adapter method. Same event either way.
* **Keymap interaction:** Alt+digit is a legal GH #1 binding today
  (`validate_combo` refuses Alt+LETTER only). Once script keys exist,
  `validate_combo` **refuses Alt+1..8 by name** (same rule and same wording as
  the Alt+letter refusal, `GH1-DEBUGGER-KEYMAP-DESIGN.md` §5) — deterministic,
  no round-robin ambiguity, no accept-with-warning special case. A config file
  that already carries one is reported as a `LoadIssue` and the action stays
  at its default (§4 of that design).
* **SDL twin:** the SDL frontend has no debugger UI but does run scripts, so
  `host_key_latch` (`src/platform/host_key_latch.h`) gains the same eight
  chords with the same swallow semantics, raising the same backend event.
* **No-script case:** with no script attached the chords are still consumed
  (the event has no subscriber and is dropped by the backend) — consuming
  only-when-a-script-is-loaded would make "does Alt+1 type a 1" depend on
  invisible state.

Rows (in the DSL PR, on the suites that own each window; names agreed with
design-dsl): `host_hotkey_test` H-SCRIPT-01..08 (Alt+N with the debugger
CLOSED raises `Host{scriptN}` and the guest matrix row for the digit stays
released, press AND release), H-SCRIPT-09 (Alt+9 / Alt+0 still reach the
guest — the reservation is exactly 1..8); `debugger_keymap_test` DKSK-01
(Alt+1 in the focused debugger window raises the event), DKSK-02
(`validate_combo` refuses `Alt+1` with a reason; `app_config_test`'s DK group
gets the file-level twin); `host_key_latch_test` HKL-SK-01 (SDL path).
`debugger_accel_test` DACC-* must stay green (no mnemonic is added).

Why REFUSE rather than accept-with-warning for a user binding on Alt+1..8: a
`QAction` shortcut in `DebuggerWindow` outranks its `keyPressEvent`, so a
debugger action bound to Alt+1 would either be AMBIGUOUS with the script
`QAction` (Qt dispatches identical sequences round-robin — the GH #124 defect
this window shipped five times) or, if the script key were delivered through
`keyPressEvent` instead, silently lose in the debugger window while winning
in the emulator window. Neither is a rule a user can predict; refusing by
name is.

## 6. Behaviour-identity test plan

### 6.1 What is pinned today (keep green; these ARE the identity test)

| Suite (rows) | Gate | Pins |
|---|---|---|
| `step_out_test` (50) | none | Step Out predicate + wiring (GH #203) |
| `resume_step_off_test` (19) | none | GH #221 step-off on every resume path incl. `step_over(next_pc)` as computed by the manager |
| `persistent_bp_test` (18) + `debugger_persistent_bp_test` (5) | none / dbg | `armed()` vs `active()`; window auto-opens on a hit (`check_breakpoint_hit`) |
| `bp_enable_test` (23), `io_watchpoint_test` (25) | none | GH #225 model/live split; GH #222 port rule |
| `debugger_inspect_watchpoint_test` (18) | dbg | panel reads (Watches/Memory/Stack/Disasm, through the REAL panels and the real 12-tick throttle) never fire watchpoints; `on_run_to_eof` used as the resume |
| `debugger_menu_test` (45) | qt+dbg | menu shape and effect: Add Execute (GH #215), context-menu data bps (GH #218), observer redraw (GH #220), IO types (GH #222), Run no-op while running (GH #223), BPEP-* panel enable flags, MBP-* magic-bp toggle |
| `debugger_video_panel_test` (106) | dbg | every layer view vs the compositor (DVP-*), raster block (DVP-RAS-*), layer state via `peek` (DVP-LS/PEEK-*), `fb_row_for_vc`, **Run to EOF/EOSL targets** (DVP-11, :1261-1330: raw VC = 255+vblank_top; EOSL in the bottom border steps one line) |
| `debugger_audio_panel_test` (15) | dbg | AY/YM + stereo labels from live signals; mute mask |
| `debugger_keymap_test` (34), `debugger_accel_test` (8), `main_window_accel_test` (5), `host_hotkey_test` (45) | qt+dbg / dbg / qt | bindings, captions, forwarding of the five keys from the main window, mnemonic uniqueness |
| `debugger_quit_gate_test` (5), `quit_cleanup_test` (7), `resume_guard_test` (11) | dbg / qt+dbg / none | Task 60e/60f corruption gate policy and the app-quit bypass |
| `debugger_disasm_copy_test` (33) | dbg | GH #21 selection/copy, symbol substitution, selection survives refresh/follow-PC/`set_paused` |
| `debugger_window_size/grow/attach_test` (21/4/32) | dbg / none | geometry — untouched by #278 |
| `rewind_test` (261) | none | core `step_back` / `rewind_to_frame` / `rewind_to_cycle` semantics, snapshot width |
| `raster_state_test` (86) | none | `raster_state_at` derivation |
| `preferences_apply_test` (56) | qt | keymap push (`push_debug_keymap`) survives Apply |

### 6.2 What is NOT pinned (gaps to close BEFORE the refactor)

Measured by `grep -rl` over `test/` (2026-09-26):

| Gap | Evidence | Row(s) to add (proposed ids) |
|---|---|---|
| **Step Over's call-like decision through the GUI verb** — `on_step_over` is invoked by no suite; `is_call_like` has no test at all | `grep is_call_like test/` → nothing | `QSO-01..06`: CALL nn, CALL cc,nn (taken and untaken), RST, DJNZ (loop taken), non-call degrades to Step Into — assert PC after the verb, not the one-shot |
| **Step Into via `debugger_step` from the verb** (halt run-out, frame bookkeeping) | `on_step_into` appears only in `ctc_interrupts_test` (as a means) | `QSI-01..03`: step at HALT advances past it; step turns the frame over (frame_num increments across enough steps); data-bp latch consumed |
| **Pause-edge UI sequence**: follow-PC on pause, the four paused-only panels stop/start updating, `snapshot_raster` before refresh | `set_paused` asserted only for disasm in GH #21 rows; no row asserts CPU/Stack/CallStack are frozen while running | `QPE-01..05`: run with a stale register label → label unchanged while running, updated on the pause edge; disasm view_addr recentres on PC at the edge; raster labels are dashes while running |
| **Throttle**: running-mode panels refresh every 12th tick | INSPW-03 relies on it as a mechanism, asserts nothing about the rate | `QTH-01`: 11 ticks → no refresh call, 12th → one (count via a NextREG write visible in the panel) |
| **Rewind UI**: slider range = [oldest,newest], thumb follows `frame_num()` only while running, status text, Frame Back = `frame_num()-1`, `can_rewind`/`can_step_back` greying incl. the RZX and trace-off cases, Enable Rewind create/resume/keep-but-pause branches, Buffer Size dialog `resize` | `on_step_back` / `on_rewind_to_frame` / `update_rewind_ui` appear in NO suite (keymap/accel matches are label strings) | `QRW-01..12` driving the real window with a real rewind buffer (fixtures exist in `rewind_test`) |
| **Step Back / Frame Back failure classes**: refused (RZX) → no modal; benign (empty/trace off) → no modal; corrupt → `warn_state_corrupt` modal + status text | `resume_guard_test` says the slot wiring is "verified by code inspection" | `QRW-13..15` with the auto-answering timer idiom from `quit_gate_test` |
| **Watches panel**: add/edit/remove, byte/word/long formatting, the three disasm context-menu add routes | only INSPW uses `add_watch` as a means | `QWP-01..06` |
| **NextREG panel edit writes through the Z80 path** (handlers run) and refresh uses `peek` (not `read`: NR 0x2C/0x2E must not latch) | DVP-PEEK covers `video_panel_layer_state`, not the panel | `QNR-01..03` |
| **Sprite, Copper, MMU, Stack, Call Stack, CPU panels**: no suite instantiates them | `grep SpritePanel\|CopperPanel\|MmuPanel\|StackPanel\|CallStackPanel\|CpuPanel test/` → nothing | `QPN-*`: one row per displayed field family per panel (sprite decode ×10 cols; copper WAIT/MOVE/NOP/HALT decode + PC highlight + window clamp; MMU page/ROM/B<n>, 7FFD bank/ROM/lock; stack 24 words + wrap guard; call stack order + symbol; CPU flags + HALTED + "Bank 7 HiRes") |
| **Memory panel**: CPU view vs slot view read/write, SP/VRAM/attr highlight | only INSPW (reads) | `QMP-01..05` incl. the slot-view "physical" read that is really a CPU-map read (`memory_panel.cpp:130-139`) — pin the CURRENT behaviour for WP0..WP7. **WP8 (owner Q7) then REPLACES the slot-view rows with `QMP-06..09`**: QMP-06a a ROM slot under an overlay: DivMMC (or Multiface ROM) mapped over slot 0 → CPU view shows the overlay bytes, slot view shows the ROM page's bytes via `MemSpace::Rom{…}`; QMP-06b a RAM slot under an overlay: Multiface RAM over slot 1 with NR 0x51 = a RAM page → CPU view shows the MF RAM, slot view shows the NR page via `MemSpace::Page{}` (both branches of the `is_rom` split witnessed — review R-3); QMP-07 an unmapped page (one in no slot) is readable through the selector; QMP-08 a slot-view write lands in the physical page and is invisible to the CPU view while the overlay is active; QMP-09 a Layer 2 write-over (port 0x123B) in slot 0/1 does not leak into the slot view. QMP-06a/06b/08 are the mutation witnesses: revert `read_byte` to the CPU-map read → all three red; feed a ROM slot's effective page into `Page{}` → QMP-06a red for the RIGHT reason (it reads RAM page `rom_page+0x20`, whose bytes the row plants to differ) |
| **Trace GUI**: toggle button ↔ menu check ↔ indicator colour; Clear; Export writes the file; Step Back greys when trace is off | `trace_log()` used as a means only | `QTR-01..04` |
| **MAP load dialogs**: count message, failure message, symbols reach the three panels | `load_z88dk_map` only in disasm_copy as a means | `QMAP-01..03` (drive `SymbolTable` directly + `set_symbol_table`; the QFileDialog is not testable, the slot body is) |
| **`set_enabled` seeds**: on enable, the four panels get the CURRENT pause state and one immediate refresh | PBPUI-02 checks visibility only | `QEN-01..02` |
| **Video tab switch** re-renders the newly visible tab only | DVP rows call `refresh()` directly | `QVT-01` |
| **`jnext_debug` is Qt-free by directory** | comment-enforced only | lint row: no `#include <Q` under `src/debug/` (and after the move, `src/debug/` has no `*_qt.h`) |

Estimated ~60 new rows, all in existing gated suites or one new
`debugger_panels_test` (gate `dbg`, offscreen QPA like its siblings), all
written against the CURRENT tree and green there before a line of #278 moves.
Every gap row is a behaviour a reviewer would otherwise have to check by hand
after the refactor.

### 6.2a WP0 as built (2026-09-28)

Where each §6.2 row landed, and every way it differs from the table above.
As first submitted, 83 rows: `debugger_panels_test` 36 (new, gate `dbg`),
`debugger_verbs_test` 46 (new, gate `dbg`), `debugger_video_panel_test` +1
(QVT-01), with no production change. The WP0 FIX ROUND (manager decision: the
defects a work package surfaces are fixed on its branch) then fixed the seven
defects listed at the end of this section, with rows pinning the fixed
behaviour: panels 39, verbs 48, video_panel +2, plus rows in `rewind_test` and
`debugger_backend_test` where the defect reached them. No test seam was added: every row reads what a user reads (label and cell
text, cell colours, menu-item enabled state, modal titles and texts, files on
disk) or where the machine stopped. The custom-painted Memory panel is read
through a recording paint device that keeps the text items its `paintEvent`
draws; the file dialogs are the REAL widget-based `QFileDialog`, answered by a
timer (so §6.2's "the QFileDialog is not testable" did not hold).

| §6.2 row(s) | Suite | As built |
|---|---|---|
| QSO-01..06 | verbs | As designed. Each row also asserts the action set (running shape while a call-like step runs, paused shape at the stop). QSO-04's restart routine is the row's own code under +3 all-RAM paging: a ROM `RST $38` ran the 48K interrupt routine on an unset-up machine and never came back. |
| QSI-01..03 | verbs | As designed, plus **QSI-04** (added): the step's own UI — CPU panel and Disassembly on the new PC with no tick in between. |
| QPE-01..05 | verbs | 01 Run freezes CPU / Stack / Call Stack / Disassembly (its listing too) and flips the actions; 02 a breakpoint's pause edge refreshes them; 03 the Disassembly re-centres; 04 raster read-out: dashes while running, the stop line after the edge's tick (the panel-only half already existed as DVP-RAS-11..14); 05 Break. Plus **QPE-06** (added after a surviving mutation): Step Over on a call, Step Out, Run to Here, Run to EOF and Run to EOSL freeze the panels exactly as Run does. |
| QTH-01 | verbs | As designed, plus **QTH-02** (added): while paused every tick refreshes. |
| QRW-01..12 | verbs | Renumbered: 01 toolbar shown only past one snapshot; 02 slider = [oldest, newest] tag and the label "Frame <newest> / <newest>" at the live end; 03 thumb at the live end while running; 04 thumb user-owned while paused; 05 "Rewind: N frames / M MB"; 06 after a jump to t, "Rewound: frame t of <newest> (F5 / Continue …)" and "Frame t / <newest>"; 07 / **07b** Frame Back button / menu, three presses from the live end = frames K, K-1, K-2; 08 Step Back button + menu; 09 slider release + Jump Here; 10 trace-off greys Step Back only; 11 RZX greys both on the next tick, running greys both; 12 Enable Rewind creates; **16** Buffer Size dialog (0 greys Step Back / Jump Here on the next tick); **17** Enable Rewind off = keep-but-pause; **18** Enable Rewind on resumes the same buffer; **19** a frame run again after a rewind keeps its number and the abandoned snapshots go. The restored machine is asserted by the HL a counter loop held at each tagged frame start. |
| QRW-13..15 | verbs | As designed: refused (RZX), benign, corrupt. |
| QWP-01..06 | panels | Split: QWP-06 is the immediate route, **QWP-07** (added) the `(rr)` route. |
| QNR-01..03 | panels | 01 an edit runs NR 0x41's handler (palette entry + index advance); 02 the display is the live composed value, not the cache; 03 a refresh, and a non-hex edit, write nothing back. The "refresh uses `peek`, not `read`" half of §6.2 was ALREADY pinned: DVP-PEEK-02 drives the real `NextRegPanel`. |
| QPN-* | panels | QPN-CPU-01..04 (02 sets F one bit at a time, review round 1), QPN-MMU-01..03, QPN-STK-01..02, QPN-CS-01..05 (03..05 INT / NMI frames and the ISR's return, fix round), QPN-SPR-01..05 (02 includes an extended 8-bit sprite, fix round), QPN-COP-01..04. |
| QMP-01..05 | panels | 01 CPU-view read; 02 CPU-view edit (ROM untouched); 03 selector labels, compared exactly (fix round); 04 slot-view READ through the CPU map, overlay included (the WP8 "before" witness); **04b** (split) slot-view WRITE through the CPU map; 05 row colours at both ends of each range. QMP-06..09 stay reserved for WP8. |
| QTR-01..04 | verbs | Step Back's greying on a trace toggle is folded into QTR-01 / QTR-02. |
| QMAP-01..03 | verbs | Through the real file dialog, plus **QMAP-04** (fix round): the Z88DK loader's failure and zero-symbol cases. |
| QEN-01..02 | verbs | 01 enabling on a paused machine shows it at once; 02 re-enabling a reused window on a running machine re-seeds it as running. |
| QVT-01 | video_panel | Paused case, plus **QVT-02** (fix round): the running case. |
| lint row | — | WP1, per the WP0 brief. |

**Defects found by WP0, fixed in the WP0 fix round** (one commit each; rows
pin the FIXED behaviour):

1. **Frame numbering in the rewind UI** (`a9c592148`). A ring snapshot is taken
   in `begin_new_frame()` after the counter counted its frame, and a restore
   clears `frame_in_progress_`, so the restored frame was counted a SECOND time
   when it ran: after `rewind_to_frame(K)` the re-run frame was tagged K+1, and
   a `step_back` inside frame K moved `frame_num()` to K+2 (and pushed a
   spurious `FrameEnded`). Fixed in the core by `Emulator::at_restored_frame_start()`
   (host-side provenance, set by the two ring restores); the ring drops the
   history a rewind left when the machine runs forward. The Qt window now names
   the current frame by snapshot tag (label, "Rewound" status, thumb) and Frame
   Back goes one frame further back per press; the backend's `run_to_frame()`
   takes its base from the flag and its `FrameEnded` detector watches the frame
   start cycle. No existing expected value changed (RTF-03, CTL-10-03 and
   WV-REWIND-01 keep theirs). Rows: `rewind_test` SB-05, RTF-04..06;
   `debugger_backend_test` F2-06, F2-07, SES-02-25; QRW-02/06/07/07b/19.
2. **`on_load_map_z88dk()` tested an `int` as a `bool`** (`f0ebc1122`): now
   `>= 0`. Rows: QMAP-04; `debugger_backend_test` SYM-10 (the backend was right).
3. **The Call Stack never showed INT or NMI, and an ISR's return emptied it**
   (`318690306`). `CallStack::on_interrupt()` had no caller; the CPU now reports
   the kind of slot it ran (`Z80Cpu::last_slot_kind()`) and the post-slot hook
   records INT / NMI frames; a return pops only the frames strictly below the
   new SP. The backend's `call_stack()` is the same tracker. Rows: QPN-CS-03..05;
   `debugger_backend_test` INS-12-03/04.
4. **An extended 8-bit sprite's pattern** (`b12ef09e4`): `get_sprite_info()`
   reports N5:N0 for an 8-bit sprite and N5:N0:N6 for a 4-bit one only
   (sprites.vhd:801-804, :816, :962-963). Rows: QPN-SPR-02; `debugger_backend_test`
   INS-08-14.
5. **A layer tab switched to while running showed a stale picture**
   (`42753859f`): the running early return now requires `last_vc_ == -1`. Row:
   QVT-02.
6. **The Memory page selector's casing** (`22bb5e3ec`): "Slot N (page XX)" with
   the hex upper case, as the user guide says. QMP-03 compares exactly.
7. **The actions were recomputed only by a verb** (`60c979520`, trimmed in
   `b67d81ceb`): while paused the manager refreshes them with the panels on every
   tick, so an RZX started from the main window, or the buffer freed by Rewind
   Buffer Size... = 0, greys Step Back / Jump Here on the next tick (running,
   every such action is already off). Rows: QRW-11, QRW-16.

**Finding against the backend (for WP3):** the Qt rewind UI now needs
`at_restored_frame_start()` — the one fact that tells the boundary BEFORE a
restored frame from the ordinary boundary AFTER a frame, which have the same
counter. The backend does not publish it; recorded as REQ-qt-09d in §8.

### 6.2b WP1 and WP2 as built (2026-09-29)

No WP0 row and no §6.1 suite changed an expected value. The fixtures that
construct a `DebuggerManager`, or a `MainWindow` whose manager a row uses, now
build the backend QtApp hosts (a `jnext::dbg::Debugger` on the fixture's
`Emulator`, declared before the window so it outlives the manager) and hand it
over — fixture code only.

| Rows | Suite | Pins |
|---|---|---|
| QTF-01..08 | `debug_qt_free_test` (new, gate none — both configurations) | WP1's lint row: no Qt include directive and no `*_qt.*` under `src/debug/`, the scan read the real directory, `src/qt/` header-only with the two headers and no `Emulator`; 06..08 prove the scanner on a planted tree (both detectors fire — the name detector on a `*_qt.h` AND a `*_qt.cpp`, review round 1; prose, a commented-out include and a string do not). §6.3 mutation 10 turns 01, 02 and 04 red |
| PBPUI-06..08 | `debugger_persistent_bp_test` | with the window closed and the adapter detached, a pause from each source still opens it on the next tick: the magic breakpoint (nothing armed until it fires), a persistent breakpoint (armed by the flag, no client), another client's `pause()` — which stays that client's pause (owner Q5) |
| PBPUI-09 | `debugger_persistent_bp_test` | closing the window after a magic breakpoint opened it DISARMS the machine (the §4.1 transitional clear of the legacy bit); the user resumes first, so the row does not pin what a close does to a machine still paused by the magic breakpoint — see the CTL-13 note in §4.1 |
| QPE-07, QPE-08 | `debugger_verbs_test` | another client's `run()` reaches the window as a resume (defect: it used to stay in the paused shape), and its `pause()` as a full pause edge |
| QPE-09 | `debugger_verbs_test` | the pause edge is an EDGE: paused ticks refresh but never re-centre the Disassembly, so a user who scrolled it keeps the view (added after a mutation that re-applied the edge on every paused tick survived the first run) |
| QEN-03 | `debugger_verbs_test` | with the window open a hard reset keeps it armed (a breakpoint at 0000 stops the rebuilt machine), the live raster on, and call-stack tracking on (a CALL stepped into on the rebuilt machine is listed) — the last a defect fix: the window used to switch tracking on directly on the `Emulator`, which a cold boot reconstructs with it off, so the Call Stack stayed empty until the window was reopened; through the backend's INS-12 it is re-applied (CTL-12 rule 2) |
| QG-06 | `debugger_quit_gate_test` | the app-quit disable still resumes a corrupt machine, now through the backend's `run()` (which refuses an unacknowledged corruption; the quit path acknowledges it itself) |
| QG-07 | `debugger_quit_gate_test` | Yes to the resume modal RESUMES — the Yes acknowledges the incident in the backend, whose `run()` would otherwise refuse — and the same incident is not asked about again |
| QG-08 | `debugger_quit_gate_test` | a corrupt but RUNNING machine (a snapshot load that failed while it ran) is asked nothing by Run / Run to EOF / Run to EOSL, each a no-op there (GH #223's ordering: never ask about a resume that will not happen) |
| PBPUI-10, PBPUI-11 | `debugger_persistent_bp_test` | (review round 1) closing the window, and a manager DESTROYED with its window still open (its parent torn down, no close), each leave the surviving backend unarmed and detached with the live raster and call-stack tracking off — the two lines of `detach_backend()` / `~DebuggerManager()` that make a closed window cost nothing, which no row saw before |
| MWD-01 | `debugger_menu_test` | (review round 1) `MainWindow::set_emulator()` without `set_debugger()` throws `std::logic_error` and builds no manager; wired, it builds one. Six Qt fixtures (`audio_gain_preferences`, `esp_status`, `load_error`, `nex_v13_dialog`, `preferences_apply`, `rzx_menu`) had silently lost their manager to the old quiet skip; they now hand over a backend. Swapping `QtApp::init()`'s `set_debugger()` / `set_emulator()` order now fails every Qt regression row at startup |
| HOST-08 | `debugger_backend_test` | `JNEXT_HOST_PROBE=order` through the real HeadlessApp: a guest hard reset raised inside the frames runs before the pump, and the client's `reset(Hard)` in that pump comes second |
| `qt-host-order-func` | regression (functional) | the same script through QtApp — B3 obligation 2: QtApp now polls the guest hard reset (and the NEX `.run` request, the same deferred-reconstruct class) in `post_frames()` BEFORE the pump, as SdlApp and HeadlessApp do; polled in `pre_frames()` it ran next tick, and a client `reset(Hard)` in this tick's pump destroyed the machine with the guest's request still pending |

### 6.2c WP3 as built (2026-09-29)

The verbs and the window's rewind, trace and corruption controls drive the
backend: `on_step_back()` / `on_rewind_to_frame()` call CTL-09/10 and warn only
on `RefusedCorrupt` (a benign `RefusedRzx` / `RefusedUnavailable` is silent, as
it was); the toolbar, the Rewind menu, Enable Rewind's three branches, the
Buffer Size dialog and the status line read ST-03 `rewind_range()`; greying is
`rewind_blocked()` — the SAME predicate the two rewind verbs refuse on — plus
`trace_enabled()`; the trace ball, Enable Trace, Clear Trace and both Export
routes are INS-13; `rewind_position()` is INS-07 `time().frame`. The window
reaches the backend through `DebuggerManager::backend()`. What still reads the
`Emulator` in `debugger_window.cpp`: the breakpoint menu (WP4c) and the two
REQ-qt-09d reads (§8).

No WP0 row and no `rewind_test` row changed an expected value. Backend changes
(manager decisions 2026-09-29, recorded in `backend.md` CAP-CTL-09/10 and
CAP-ST-03): the rewind verbs are not gated on CTL-11; their `RefusedCorrupt`
means a restore tore the machine (the corruption generation moved), every benign
refusal is `RefusedUnavailable`; `resize_rewind_buffer(0)` frees the ring
(ST-03-07's expected value flipped).

| Rows | Suite | Pins |
|---|---|---|
| CTL-09-02..04, CTL-10-05..08 | `debugger_backend_test` | the classification: trace off / empty and a frame with no slot (a gap) are benign `RefusedUnavailable` and move nothing; a torn restore is `RefusedCorrupt` with the incident naming the subsystem; a rewind from a corrupt machine to an intact frame succeeds and clears the corruption; a torn NEWEST slot no longer fails rewinds to other frames |
| ST-03-07 (flipped), ST-03-09/10 | `debugger_backend_test` | `resize_rewind_buffer(0)` frees the ring (capacity and depth 0, rewinds refused as unavailable); a later non-zero resize creates a fresh ring that records |
| OBL3-01..03 | `debugger_backend_test` | B3 obligation 3: a remote client's `step_back()` / `rewind_to_frame()` then its detach leave the machine unarmed, unattached, the raster walk off and running; a replay with nothing attached still lands on its target and leaves nothing armed |
| INS-13-14 | `debugger_backend_test` | the trace export writes every `TraceEntry` field, B4's `(SP)`, I, R, IM, IFF1, IFF2 and MMU pages included, in the documented column order |
| QRW-20 | `debugger_verbs_test` | through the window: after a torn rewind (the "Rewind Failed" modal), a rewind to an intact frame succeeds with no modal and the corruption is gone |
| QTR-05 | `debugger_verbs_test` | a behaviour change, stated: the trace switched on from the window now survives a hard reset (INS-13 is client intent the backend re-applies, CTL-12 rule 2); it used to be reconstructed off |

Defects fixed with those rows: the benign-as-corrupt classification (CTL-09-02/03,
CTL-10-05); `Emulator::rewind_to_frame()` restored the NEWEST snapshot before
looking the target up, so a gap moved the machine on a "benign" refusal and a
torn newest slot failed every rewind (CTL-10-05, CTL-10-08); the export dropped
B4's fields (INS-13-14); the stuck `active()` bit after a client's rewind
(OBL3-01/02).

### 6.3 Mutation checks for the #278 reviewer

Each mutation is applied to the REFACTORED tree, in its own build dir, and
must turn at least one named row red (`feedback_mutation_harness_reports_absence`:
prove the harness sees a known mutation first):

1. `step_over()` in the backend: drop the DJNZ case → `QSO-05` (the §6.2 order puts RST at 04; corrected by WP0).
2. `run_to_end_of_frame()`: target `FB_HEIGHT-1` without `vblank_top` → DVP-11.
3. Pause epoch: increment on resume as well → `QPE-*` (panels refresh while running). **As built there is no epoch (§4.1):** the equivalent mutation applies the PAUSE edge whenever the pulled state differs from the shown one — i.e. on a resume too → QPE-07.
4. `render_layer(ULA_PRIMARY)`: follow the live bank instead of forcing it → DVP-03.
5. `nextreg_peek` → `read` → DVP-PEEK-02 (NR 0x2D latch); `nextreg_peek` → the raw cache → `QNR-02` (WP0: the latch half was already DVP-PEEK-02's).
6. `read_memory` inside a guest scope → INSPW-01.
7. Observer notification dropped from `add_pc` → GH220-*.
8. `rewind_to_frame` outcome collapsed to bool → `QRW-13..15`.
9. Detach while paused stops resuming → QG-02 / PBPUI.
10. Move `debug_keymap_qt.h` back under `src/debug/` → the lint row.

Plus the standing gates: `make unit-test`, `make unit-test-sdl`, FUSE
1356/1356, `JNEXT_TEST_JOBS=4 make regression`, `make build-matrix` (all four
configurations — the header move is exactly the kind of change that breaks the
`QT_UI=OFF, DEBUGGER=ON` link).

---

## 7. Work packages for #278

Prerequisite (serial, before anything else): **WP0 — close the §6.2 gaps on
the current tree** and land them (they are green on both trees by
construction). Then, in dependency order:

| WP | Content | Depends on | Parallel? |
|---|---|---|---|
| WP1 | Header move: `src/qt/` + 6 include edits + CMake sanity across the build matrix + lint row — **Q WP1 is the single owner of this move** (backend B6 removed, arch §10) | WP0; backend B0 (the four public headers) lands first and alone — WP1 does not depend on it but follows it in order | yes (independent of the backend API) |
| WP2 | `DebuggerManager` control verbs onto the backend: attach/detach, run/pause, step into/over/out, run-to, EOF/EOSL, epoch-based edge detection, `apply_pause_state`; delete the disassembly and cycle arithmetic from the manager | backend CAPs for REQ-qt-01..07, 15 | serial — the manager is one file and every panel WP touches its wiring |
| WP3 | Rewind + trace + corruption: `on_step_back`, `on_rewind_to_frame`, window rewind toolbar/menu/dialog, trace menu, `update_actions` inputs | REQ-qt-08..11 | after WP2 |
| WP4a | Register-family panels: CPU, MMU, Stack, Call Stack | REQ-qt-16..18, 24, 25 | yes, with 4b/4c/4d |
| WP4b | Register-file panels: NextREG, Sprites, Copper, Audio | REQ-qt-19..23 | yes |
| WP4c | Breakpoints panel + Disasm gutter/context menu + window Breakpoints menu + Watches panel | REQ-qt-13, 14, 17 | yes |
| WP4d | Video panel: `render_layer` + raster/layer-state/palette accessors; `VideoLayerView` shrinks to a wrapper | REQ-qt-25..28 (the backend's biggest item — NEEDS-PROTOTYPE candidate: move `render_to_image` + `replay_*` verbatim into a Qt-free function first and re-run DVP) | yes |
| WP5 | Memory panel (paint-time reads through `peek(Cpu)`, bulk read) — slot view still through the CPU map here, for identity; the physical-page switch is WP8 | REQ-qt-17 | yes with WP4 |
| WP6 | Symbol table ownership move + `on_load_map_*`; magic bp menu; `MainWindow` forwarding unchanged | REQ-qt-12, 14 | after WP2 |
| WP7 | Remove `core/emulator.h` from every `src/debugger/*.cpp`; final reach-around grep = 0; developer-guide chapter 3.9 + `FEATURES.md` unchanged in substance, paths updated | all | serial, last |
| **WP8** | **Owner Q7 (2026-09-27): the Memory panel's "Slot N (page P)" view becomes a true physical read/write through CAP-INS-02**, branching on CAP-INS-03 `SlotInfo.is_rom` (or `SlotInfo.space`, REQ-qt-31): RAM slot → `Page{nr_page}`, ROM slot → `Rom{…}` (`read_byte`/`write_byte`, `memory_panel.cpp:123-154`; the selector label from CAP-INS-03 stays). **The `MemSpace::Rom` enumeration (backend §11 item 1) is now closed from the code and `SlotInfo.space` is published (REQ-qt-31 accepted), so WP8 has no prototype dependency left; it still lands last, after WP7.** The ONE deliberate behaviour change in #278, so it lands LAST, on top of the proven identity; its own rows in §6.2 (`QMP-06a/06b, 07..09`); the user guide page that describes the panel's page selector, `src/doc/user-guide/06-debugger/panels/04-memory.md` (regenerate the committed render with `make docs-userguide`; `doc/man/jnext.1.md` names the hex editor but not the selector — update it only if the new wording says "physical page"); `FEATURES.md` Debugger bullet updated | WP7 green | serial, after WP7 |

**Inherited from backend package B3 (2026-09-28 — two obligations Q owns):**

1. **Retire the Qt half of `emulator_cold_boot()`'s save-and-restore** (inventory
   row 95). It carries the panels' PC breakpoints and watchpoints, the
   `BreakpointSet` observers that keep `BreakpointPanel` / `DisasmPanel`
   subscribed across a cold boot (each registers once, in its constructor), and
   `active()`. Before Q those have no other owner, so B3 retired only the half
   that was backend state (the event-mask bytes) and left this one standing.
   When WP2/WP6 make the panels backend clients, their breakpoints become
   subscriptions the backend re-applies (CAP-CTL-12 rule 2), and the restore
   becomes a second owner: delete it in the same WP, with the backend's rows
   CTL-12-16/17 and CTL-12-34/35 (which pin today's carried state) re-pinned
   against the client model.
2. **Poll `take_hard_reset_request()` in `post_frames`, before `pump()`.** Qt
   polls it in `pre_frames` today (`qt_app.cpp:510`) while the pump slot is
   `post_frames` (`:667`), so a guest NR 0x02 raised in tick N's frames is seen
   in tick N+1 — after tick N's pump. A client `reset(Hard)` in that pump
   destroys the machine and the pending flag with it: the guest reset is
   silently subsumed instead of "run first, then the client's", which is what
   CAP-CTL-12's ordering paragraph promises. SDL and headless already poll after
   their frames and are ordered correctly.
3. **`active()` is also set by the rewind paths.** `Emulator::rewind_to_cycle()`
   and `rewind_to_frame()` call `debug_state_.set_active(true)` (on success and
   on their failure paths), so a backend client's `step_back()` /
   `rewind_to_frame()` sets the Qt WINDOW's bit, which no client detach ever
   clears: after one remote step-back the machine stays armed, and the raster
   walk and render hint stay on, for the rest of the session. Pre-existing — B3
   did not touch it (confirmed by B3's contract review) — and retired together
   with `active()` when the Qt window becomes a client. Since B3 that bit is also
   a term of `DebugState::attached()` (the step-machinery gate) and
   `raster_live()` (the raster walk and render hint), so the stuck bit holds
   those on too — the same things `active()` gated directly before B3, now
   reached through the two bits that replaced it in the hot path.

**Status of the three (WP2 and WP3, 2026-09-29):**

1. **Not yet due — WP4c.** The panels' breakpoints are still the
   `BreakpointSet` they observe; they become subscriptions in WP4c, and the
   restore is their only owner until then. Its `active()` half now carries only
   the bit the magic hook sets (WP3 retired the rewind paths' writes) — the
   window's own arm is a backend client since WP2, which the backend re-applies
   across a cold boot (QEN-03) — and it retires with that bit, the magic hook's
   write and the window's clear, in WP4c.
2. **Done in WP2** — both deferred reconstructs (hard reset, NEX `.run`) are
   polled in `post_frames()` before `pump()`; rows HOST-08 and
   `qt-host-order-func` (§6.2b).
3. **Done in WP3.** `Emulator::rewind_to_cycle()` / `rewind_to_frame()` no
   longer call `set_active(true)` on any path: a restore only pauses, and the
   replay is armed by `DebugState::ReplayArmScope` for its own loop, restoring
   whatever armed the machine before. A client's `step_back()` /
   `rewind_to_frame()` followed by its detach leaves the machine unarmed and
   running (OBL3-01/02); a replay with nothing attached still stops on its
   target (OBL3-03).

**Branch discipline (review R-7; owner rule 2026-09-24, arch §10.3):** #278
is one multi-stage issue and lives on **one** branch, `gh278-qt` (arch
§10.1). WP1..WP7 are commit series or short-lived sub-branches OF `gh278-qt`
(parallel WPs each in their own worktree off `gh278-qt`, merged back into
`gh278-qt` by the manager), each independently reviewed at WP granularity,
and `gh278-qt` is merged to `main` **once**, after WP7, with the full gate
(triplet + `unit-test-sdl` + `build-matrix`) and a single `make bump-patch`.
The one exception is **WP0**: its rows pin the CURRENT tree and are not part
of the refactor, so they land on their own branch to `main` before
`gh278-qt` is cut — that is what makes them the identity witness. WP4a-d and
WP5 can run as four agents; WP2/WP3/WP6/WP7 are serial on the manager.

---

## 8. REQ ledger (to `design-backend`)

Sent as `REQ-qt-<n>: <capability> — <why> — <site>`; answers recorded here.

| REQ | Capability | Status vs backend.md v1 |
|---|---|---|
| 01 | attach()/detach() — `debugger_manager.cpp:92-93,149-150` | served: CAP-SES-01/05, CAP-INS-12 |
| **01b** | per-client `set_live_raster(cid, bool)` — the adapter stays attached; window show/hide toggles the render hint — `:90-99, :152-155` | **ACCEPTED** → CAP-SES-05 per-client, OR'd into the render hint + raster walk. As built (WP2): the adapter attaches and requests the live raster while its window is open (§4.1) |
| **01c** | finding: `active()` gates the step machinery too (`debug_state.h:17`, `emulator.cpp:9251, 9916`); CAP-SES-05 must not make step-out / step-back depend on `live_raster` | **ACCEPTED** → two flags: `attached` gates the step machinery, `live_raster` only the render hint/raster walk |
| 02 | paused()/pause()/resume() — `:326-375` | served: CAP-CTL-01/02/13 |
| 03 | step_into() = `debugger_step()` — `:391` | served: CAP-CTL-03 |
| 04 | step_over() decided in the backend — `:421-447` | served: CAP-CTL-04 |
| 05 | step_out() — `:458-459` | served: CAP-CTL-05 |
| 06 | run_to(addr) — `:197` | served: CAP-CTL-06 |
| 07 | run_to_end_of_frame()/…scanline() — `:493-546`, DVP-11 | served: CAP-CTL-08 |
| 08 | step_back(n)/rewind_to_frame(n) — `:562-622` | served: CAP-CTL-09/10 |
| **08b** | `Result::RefusedUnavailable` (buffer empty / trace off / frame out of range) distinct from `RefusedCorrupt` — `warn_state_corrupt` must not fire on a benign failure (`:275-282`) | **ACCEPTED** → `Result::RefusedUnavailable` |
| 09 | rewind buffer control/range, frame number — `debugger_window.cpp:580-614, 774-890` | served: CAP-ST-03, CAP-INS-07 |
| **09b** | `snapshot_bytes()` — `:816, :850` | **ACCEPTED** → `rewind_range()` = {oldest, newest, depth, capacity_frames, snapshot_bytes} |
| **09c** | `rewind_blocked()` pre-query for greying — `:709-714` | **ACCEPTED** → `rewind_blocked() -> optional<string reason>` |
| **09d** | "does the machine sit on a restored frame start?" (`Emulator::at_restored_frame_start()`, WP0 fix round) — Frame Back's target and the "Rewound" status need it (`debugger_window.cpp` `frame_back()`, `update_rewind_ui()`); the backend's own `run_to_frame()` already reads it | **WITH THE OWNER** (WP3, 2026-09-29): a public-header change — `bool at_restored_frame_start = false;` in `RewindRange` (ST-03). It is NOT derivable from the published API: a restored start of frame K and the ordinary boundary after K are identical in `time()`, `state()` and `at_frame_boundary()` except for `master_cycle`, and the start of K can only be computed from `rewind_range()`'s newest snapshot by assuming a fixed frame length, which an NR 0x03 timing or NR 0x05 50/60 Hz switch inside the ring's span breaks (manager: do not take the derivation). Until the owner answers, the two reads stay in `debugger_window.cpp`, each marked "WAITING ON REQ-qt-09d" |
| 10 | trace enable/export — `:214-244, 542-574` | served: CAP-INS-13 |
| **10b** | `trace_enabled()` query + `trace_clear()` — `:218, 558, 755` | **ACCEPTED** → CAP-INS-13 |
| 11 | corruption observables — `debugger_manager.cpp:279-303` | served: CAP-CTL-11 |
| **12** | `magic_breakpoint()` / `set_magic_breakpoint(b)` — `main_window.cpp:930-936`, MBP-01/02 | **ACCEPTED** → new CAP-CTL-14 |
| 13 | breakpoint model + mutators + observer — §3.5 | served: CAP-EVT, CAP-INS-17, CAP-SES-02 |
| **13b** | `SubscriptionsChanged` carries the kind (PC vs data) — `disasm_panel.cpp:143-147`, GH #220 | **ACCEPTED** → `SubscriptionsChanged{kinds: bitmask}` |
| **13c** | READ_WRITE as one listable/editable row — kind bitmask or adapter pairing | **ACCEPTED** → memory subscriptions carry an `access` bitmask {Read, Write}; one row |
| **13d** | GUI subscriptions: `owner = client`, no condition, `once=false`, `Stop`; panel lists all owners | **CONFIRMED**; a client edits only its own rows — script/remote rows are read-only in the panel (settles §9.2 as design) |
| 14 | symbol table in the backend — `debugger_manager.h:131` | served: CAP-SYM |
| 15 | pause/resume transitions — `:682-720` | served: CAP-SES-02 + CAP-CTL-13 |
| **15b** | listener contract: the Qt listener records only; UI work deferred to the tick (§4) — no backend change, recorded so the backend does not assume UI-in-callback | note — **as built (WP2): no listener; the tick pulls `state()` (§4.1)** |
| 16 | registers() — `cpu_panel.cpp:199` | served: CAP-INS-01 |
| 17 | memory peek (non-perturbing, F1) / poke — `memory_panel.cpp:128-153` | served: CAP-INS-02 |
| **17b** | confirm `poke(Cpu)` ≡ `Mmu::write` minus watchpoints | **CONFIRMED** (outside `GuestExecutionScope`; returns count + `RefusedReadOnly`, which the GUI ignores) |
| 18 | MMU view — `mmu_panel.cpp:116-130` | served: CAP-INS-03 |
| 19 | nextreg peek / write — `nextreg_panel.cpp:174,206` | served: CAP-INS-04 |
| 20 | sprite_info ×128 — `sprite_panel.cpp:68` | served: CAP-INS-08 |
| 21 | copper view — `copper_panel.cpp:101-114` | served: CAP-INS-09 |
| 22 | AY regs — `audio_panel.cpp:178` | served: CAP-INS-10 |
| **22b** | `turbosound_enabled()`, `ay_mode()`, `stereo_mode()` live signals — `:187-209`, DAP-02..06 | **ACCEPTED** → CAP-INS-10 |
| 23 | mute mask — `:105,165` | served: CAP-INS-10 |
| 24 | call stack — `callstack_panel.cpp:49` | served: CAP-INS-12 |
| 25 | raster_state() when paused; ULA mode/shadow bits — `video_panel.cpp:281-285`, `cpu_panel.cpp:249-250` | served: CAP-INS-06, CAP-INS-15 |
| **25b** | raster geometry in `machine_timing()`: hc_max, vc_max, max_hblank, max_vblank, display_origin, vblank_top — `:997-1005` | **ACCEPTED** → new CAP-INS-19 `machine()` (type + timing + raster geometry + fps) |
| 26 | layer_state() | adapter-side from CAP-INS-04 — no REQ |
| 27 | ULA palette — `:1060-1062` | served: CAP-INS-15 |
| **27b** | active ULA palette bank + one RGB333→ARGB function | **ACCEPTED** → `PaletteId::UlaActive`, `active_ula_palette_bank()`, `rrrgggbb_to_argb` re-exported from `inspect.h` |
| 28 | render_layer — `:394-630` | served: CAP-INS-14; split per §3.7 **NEEDS-PROTOTYPE** (agreed: verbatim move, re-run DVP first — WP4d step 1) |
| **30** | WP8 contract on CAP-INS-02: `peek(Page{p})` returns the NR page's bytes regardless of any DivMMC/Multiface/L2 overlay over the slot; `poke(Page{p})` writes it, invisible to an overlay. **Reworded (review R-3):** `Page{p}` is used for RAM slots only; a ROM slot's bytes come from `MemSpace::Rom{…}` and its `poke` is `RefusedReadOnly` (panel renders "unchanged") — `memory_panel.cpp:123-154`, owner Q7 | **CONFIRMED** (backend CAP-INS-02 / §4.2a; matrix: Qt 39 used / 16 declined, INS-02 Page = S via Q WP8). The backend also logs every mutation as one SES-06 line `MUTATE <what> <old> -> <new> by <client>` — no panel change needed |
| **31** | `SlotInfo` should carry the `MemSpace` that reads the slot's backing store (`space ∈ {Page{nr_page}, Rom{index}, bank7-BRAM…}`) so the Memory panel — and every other client — never composes a `MemSpace` from `effective_page` + `is_rom` (that composition is exactly what R-3 caught: `get_effective_page()` is SRAM-physical for ROM slots, `mmu.h:74-77`). Alternatively: settle the `Rom` enumeration (backend §11 item 1) and state the rule "ROM slot ⇒ `Rom{effective_page}`" explicitly. WP8 depends on one of the two | **ACCEPTED** (verified by the backend): CAP-INS-03 `SlotInfo.space` = `Page{nr_page}` for a RAM slot, `Rom{effective_page}` for a ROM slot; backend §11 item 1 closed from the code (`Rom{i}` = 8 KB ROM page index: SRAM pages 0..7 in Next mode, the `Rom` object's pages on 48K/128K/+3; `poke(Rom)` = `RefusedReadOnly`). WP8 branches on `SlotInfo.space` directly; the `is_rom` split is equivalent. **Refinement (backend, after the protocols review):** `Rom{index}` is a 16 KB ROM image (index 0..3, addresses 0..0x3FFF; SRAM pages 2i/2i+1 on the Next, the `Rom` object's image on classic machines), and `SlotInfo` carries `space` + `space_offset` (ROM slot → `Rom{effective_page >> 1}`, offset `(effective_page & 1)·0x2000`; RAM slot → `Page{nr_page}`, offset 0). WP8's read is `peek(space, space_offset + addr_in_slot, …)`; the 8 KB slot view and the QMP-06a/06b/07/08/09 rows are unaffected (they address bytes within the slot, never the image) |
| **32** | a NON-ARMING observer attach — a client that installs a listener and counts toward no arm bit (a `ClientInfo` flag, or an `observe()` verb). SES-01's attach is the only way to become a client and it ARMS the machine (§5), so the Qt adapter cannot be a client for the process lifetime without changing GH #219's default (PBPUI-03, §4.1); B4's O5 is the same class for a loop owner and `ExitRequested` | **OWNER-APPROVED 2026-09-29** — a change to the frozen `debugger.h`, to be made when package S (the DSL) or a remote server first needs a non-arming listener; not in Q unless Q needs it (it does not: WP2 attaches while the window is open and pulls `state()`, manager decision 2026-09-29) |
| **29** | CAP-CTL-15 `load(path)` must preserve every client's subscriptions, the master switch and the attached/live_raster state across the destroy/reconstruct, exactly as `emulator_cold_boot()` does for `BreakpointSet` + `active()` today — `src/platform/emulator_boot.h:133-146` (review N-10) | **ACCEPTED** (verified by design-backend): CAP-CTL-15 `load()` and CAP-CTL-12 `reset(Hard)` share the reconstruct contract — subscriptions, enable flags, master/per-client switches, attached/live_raster and the symbol table are kept outside `Emulator` and the hooks re-installed after the placement-new; a backend row pins it |

MAPPED against backend.md v3 + the owner review of 2026-09-27 + round 4:
**40 CAP ids used (35 of v1, +3 additions CAP-CTL-14 / CAP-INS-19 / per-client
CAP-SES-05, + CAP-INS-02 `MemSpace::Page` and `MemSpace::Rom` per Q7/R-3), 15
declined (§3.3), 0 REQs open, 0 reach-arounds (backend v7).** (Since then the WP0 fix round opened REQ-qt-09d, and WP2 raised REQ-qt-32, owner-approved 2026-09-29.) All 14
sub-REQs ACCEPTED/CONFIRMED; REQ-qt-28 is NEEDS-PROTOTYPE by agreement
(§3.7). To be re-confirmed as "MAPPED" against v2 when broadcast (additions
only expected).

---

## 9. Open questions for the owner

None open.

Settled by the owner (2026-09-29, GH #278 milestone-1 review):
* **CTL-13 — closing the Qt debugger window while a `Magic` pause holds the
  machine: it resumes, as it always has ("ctl-13: agree").** The window's close
  resumes whoever paused; the no-owner rule for `Magic`/`Corrupt` binds only
  SES-01's `detach()`. Architecture §4.1 CTL-13 amended to say so (§4.1 above).
* **REQ-qt-32 — a non-arming observer attach: approved**, to land with package
  S or the first remote server that needs it, not in Q (§8).

Settled by the owner (review 2026-09-27):
* **Q7 — Memory panel "slot view" becomes a TRUE physical-page read.**
  Today `memory_panel.cpp:130-139` reads `(slot<<13)|(addr&0x1FFF)` through
  the CPU map, so "Slot 3 (page 0A)" is that page only because the slot is
  mapped there, and a DivMMC/Multiface/Layer-2 overlay active in the slot
  leaks into the "page" view. Decided: the view reads (and `write_byte`
  writes) through CAP-INS-02 — `MemSpace::Page{nr_page}` for a RAM slot,
  `MemSpace::Rom{…}` for a ROM slot (a ROM slot's "effective page" is an
  SRAM-physical index outside `Page{}`'s NR number space, review R-3) — so
  the label means what it says, overlays no longer leak
  in, and an unmapped page is readable. Delivered as **WP8**, the LAST work
  package of #278, after WP0..WP7 are green — it is the one deliberate
  behaviour change in the issue, so it lands on top of the proven identity,
  with its own pinned rows (§6.2, §7).
* **Q5 — a remote client's pause opens the Qt debugger window: yes.** Today's
  GH #219 path (`debugger_manager.cpp:691-693`) is unchanged; the adapter
  never inspects `Paused{by}` to suppress it.
* **Q13 — Alt+1..8 are the DSL host keys in both windows: yes.** Delivery as
  §5.3.

Settled as design, not owner questions:
* script/remote-created subscriptions ARE listed in the Breakpoints panel,
  marked by owner, read-only (REQ-qt-13d confirmed) — DSL/remote PRs, not #278;
* design-dsl's "Script" tab and Alt+1..8 host keys are post-#278 (CAP-EVT
  `Host` names them `script1..8`); their Qt delivery — routed independently
  of `is_enabled()`, swallowed from the guest, refused as keymap bindings,
  SDL twin, and the rows that pin each — is specified in §5.3 (review R-6);
* `DebuggerManager::emulator()` (`debugger_manager.h:64`) has no caller —
  deleted in WP2.

---

## 10. Cross-frontend notes

* **design-dsl** (received 2026-09-26): (1) shared breakpoint model — agreed
  under the four constraints in §3.5; (2) script `stop` in GUI mode = the
  GH #219 path (pause → epoch → adapter auto-enables the window, follow-PC),
  reason string via `pause_reason()` shown in the debugger status bar; a
  "Script" tab is acceptable but belongs to the DSL PR, not #278; (3) Alt+1..8
  — no known collision, see §9.3; menu rows "Load Script…/Unload Script" are
  DSL-PR menu wiring, and `debugger_accel_test` DACC-* will need the new
  mnemonics checked.
* **design-dzrp** (agreed 2026-09-26): DZRP has NO step verbs — DeZog
  computes instruction lengths and temporary breakpoints itself
  (`remotebase.ts:1545-1635`) and sends `CMD_CONTINUE`, so the adapter never
  calls `step_over()`/`step_into()`; the HALT run-out of `debugger_step()` is
  GUI/DSL-only behaviour (DeZog stepping through a HALT shows PC not moving,
  as with CSpect). DZRP temporary targets are `owner=internal` and never
  listed (same class as the GUI's Step Over one-shot); DZRP user breakpoints
  are listed read-only. A GUI Run while DeZog believes the machine is paused
  is DeZog's problem (no `Resumed` notification in the protocol; its next
  `CMD_CONTINUE` finds `run()` a no-op per GH #223). A DZRP-originated pause
  opens the local window unless the owner says otherwise (§9.2).
* **design-gdb**: the two properties it relies on — the GUI reacts to a pause
  it did not cause, and there is one breakpoint model shown in one panel —
  are kept (REQ-qt-01b, REQ-qt-13d); gdb's step targets must be
  `owner=internal`.
* **design-backend**: all 14 sub-REQs answered (§8); `render_layer` split
  agreed as NEEDS-PROTOTYPE (§3.7) and is WP4d's first step.

---

## 11. Review dispositions (independent review 2026-09-26, `scratchpad/reviews/dsl-qt.md`)

| Item | Disposition | Where |
|---|---|---|
| R-7 per-WP merges to `main` | **Accepted.** Verified against the owner rule (2026-09-24, one branch per multi-stage issue) and arch §10.1/§10.3. WP1..WP7 are now commit series / sub-branches of `gh278-qt`, reviewed per WP, merged once; WP0 alone lands first. | §7 |
| R-6 Alt+1..8 delivery | **Accepted.** Verified: the forwarding block is `is_enabled()`-gated (`main_window.cpp:2200`) and Alt+digit reaches the guest as the digit (`keyboard.cpp:361-363`). Delivery specified: independent block before the guard, swallow on press and release, `DebuggerWindow` `QAction`s, `validate_combo` refuses Alt+1..8, SDL `host_key_latch` twin, six named rows. Owned by the DSL PR (arch S-WP5); design-dsl informed. | §5.3, §9 |
| N-10 missed rows | **Accepted.** Rows #94 (`qt_app.h:126` → CAP-CTL-13) and #95 (`emulator_boot.h:133-146` → CAP-CTL-15) added; REQ-qt-29 accepted by design-backend (load/hard-reset reconstruct contract). | §1.16, §8 |
| N-11 "3 write paths" | **Accepted.** Qualified: three panel-originated edits; #82/#83 rewrite the whole machine, #76-#81 execute guest code. | §1.16 |
| Round 4 R-3 (ROM slot page space) | **Accepted.** Verified `mmu.h:74-77` / `mmu.cpp:396-402`: a ROM slot's effective page is SRAM-physical, outside `Page{}`'s NR space. WP8 now branches on `is_rom` (RAM → `Page{nr_page}`, ROM → `Rom{…}`), REQ-qt-31 (`SlotInfo.space`) accepted and §11 item 1 closed; QMP-06 split into 06a (ROM slot under DivMMC) and 06b (RAM slot under MF RAM); REQ-qt-30 reworded; `MemSpace::Rom` moves from declined to used (40/15). | §3.2, §3.3, §6.2, §7, §8, §9 |
| Round 4 N-10 (NextREG trace) | **Accepted.** Verified `nextreg.cpp:456-457`: identical trace for every caller; reworded — the SES-06 `MUTATE` line is the distinguishing record. | §1.16 |
| Owner review 2026-09-27 (Q5/Q7/Q13) | **Recorded.** Q7 → WP8 + `QMP-06..09` + CAP-INS-02 `MemSpace::Page` used (39/16); Q5 → §9; Q13 → §5.3 unchanged; write-path subtleties stated in §1.16 for the backend's write CAPs. | §1.16, §3, §6.2, §7, §9 |
| N-9 countdown copy ownership | **Accepted, closed.** #278 does not own it (it declines CAP-IN/CAP-CAP); arch §8 now assigns the retirement to the backend's B4. | §3.3 |
