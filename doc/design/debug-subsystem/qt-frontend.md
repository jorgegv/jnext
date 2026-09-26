# Qt GUI frontend — inventory, projection and refactor plan (GH #278)

Status: **v3 (final for the design round) — inventory complete; projection mapped onto `backend.md` v1 plus design-backend's verdicts of 2026-09-26 (all 14 sub-REQs ACCEPTED/CONFIRMED, §8). Re-verified against `backend.md` **v3**: every one of the 38 CAP ids cited in §3.2 exists there, and all 14 REQ-qt verdicts are recorded in its §12. MAPPED against v3: 38 used, 17 declined, 0 open, 0 reach-arounds.**
Owner of this file: the `design-qt` agent. Sibling files: `backend.md`,
`dsl.md`, `dzrp.md`, `zrcp.md`, `gdb.md` (read, never edited from here).

Everything below is measured against the code at `gh276-design` =
`main` @ `974b0ab19` (v1.0.44). File:line citations are into that tree.
`doc/design/EMULATOR-DESIGN-PLAN.md` §5.10 was not consulted as a source.

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

Rows past `vc` are left unrendered; only the VISIBLE tab's view is refreshed (:1067-1073); a tab switch invalidates and refreshes immediately (:953-959). The rendered `QImage` is the test seam (`video_panel.h:47`).

### 1.14 DebuggerWindow — `debugger_window.cpp` (menus, toolbar, dialogs)

| # | Access | Data / verb | Line |
|---|---|---|---|
| 62 | `trace_log().enabled()` / `.set_enabled(b)` / `.clear()` / `.export_to_file(path)` | Trace toggle button + Debug ▸ Trace submenu (Enable / Clear / Export…) | 218-238, 547-570, 755 |
| 63 | `rewind_buffer()` `->empty() ->depth() ->oldest_frame_num() ->newest_frame_num() ->snapshot_bytes()` | Frame Back guard, slider range, status text "Rewind: N frames / M MB", "Rewound: frame a of b", size dialog | 259-263, 511-515, 582, 706-718, 777-816, 843-851 |
| 64 | `frame_num()` | Frame Back target = `frame_num()-1`; slider thumb; labels | 261, 513, 794-811 |
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

**Inventory size: 93 numbered rows; 21 core object types; 3 write paths into
the machine** (#16 NextREG write, #46 memory write, #18 mute mask — the last is
explicitly not machine state, `emulator.h:680-691`).

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
| **Frame Back** (window :255-265, :509-517) | buffer non-empty | `on_rewind_to_frame(frame_num()-1)` (0 floor) | as Step Back |
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
| 71-72, 89 | attach/detach; the "active" render hint; call-stack tracking on while attached | CAP-SES-01, CAP-SES-05, CAP-INS-12 (`set_enabled`) | **REQ-qt-01b**: per-client `set_live_raster(cid, bool)` — the Qt adapter stays attached for the process lifetime (§4) and toggles this when the window shows/hides; **REQ-qt-01c (finding)**: `DebugState::active()` also gates the STEP machinery (`OUT`, `STEP_BACK`, `RUN_BACK_TO_CYCLE` — `debug_state.h:17`, `emulator.cpp:9251,9916`), so CAP-SES-05 must not tie those to `live_raster` |
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
| 8, 21, 33, 45 | memory read, non-perturbing (incl. F1: no +3 floating-bus latch) | CAP-INS-02 `peek(MemSpace::Cpu, …)` | — |
| 46 | memory write through the CPU map | CAP-INS-02 `poke(MemSpace::Cpu, …)` | **REQ-qt-17b (confirm)**: `poke(Cpu)` = `Mmu::write` semantics (ROM ignored, per-scanline logs and attribute mux updated, no watchpoint) — the memory panel's identity depends on it |
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
| CAP-INS-02 `MemSpace::Page` / `Rom` | the Memory panel's slot view reads the CPU map today (§9.1) — pinned as-is for identity |
| CAP-INS-03 `set_mmu_slot` | no MMU editing in the GUI |
| CAP-INS-05 `port_in/out` | no port I/O in the GUI |
| CAP-INS-08 pattern RAM / sprite palette / clip | the Sprites panel shows attributes only |
| CAP-INS-13 `trace_entries()` | the GUI exports to a file; it never lists entries |
| CAP-INS-14 `framebuffer()` | the emulator window reads it directly; not a debugger-panel path |
| CAP-INS-16 `input_state()` | recorder-only |
| CAP-EVT conditions, ranges (`lo != hi`), `once`, `Log`/`Continue`, `NextRegWrite`, `Frame`, `Scanline`, `Cycle`, `Reset`, `Host`, physical-page filter | the GUI creates single-address `Execute` / `MemRead` / `MemWrite` / `PortRead` / `PortWrite` subscriptions with `Stop` and no condition — nothing else exists in its dialogs |
| CAP-TIME-02/03 | no GUI control for them |
| CAP-IN-01..04, CAP-CAP-01..03 | input injection / capture are CLI and script paths; the main window's own screenshot is not a debugger feature |
| CAP-ST-01/02 bookmarks | no GUI control; Save Snapshot is the JNS path (#27), unchanged |
| CAP-SES-03 `pump`, CAP-SES-04 stop policy | **used by `QtApp`** (the loop owner), not by the panels: `pump(0)` per tick next to today's `check_breakpoint_hit()` call (`qt_app.cpp:666`), policy `Pause` |

### 3.4 Reach-arounds — count against v1

Of the 93 inventory rows, **91 map onto a v1 CAP** (with the 14 sub-REQs of
§8, all answered, making the mapping exact) and **2 stay GUI-side by design**
(Watches display list; layer-state derivation from four peeks).
**Reach-arounds remaining = 0; REQs open = 0.** The implementation gate is
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
| ctor takes `Emulator*` (`:23`) | ctor takes the backend session (`DebugSession&` or whatever `backend.md` names it) |
| `emulator()` accessor (`:64`) | removed — no caller in `src/` (measured: `grep -rn '->emulator()' src/` finds none from the debugger) |
| eleven copies of the four `set_paused()` calls | one `apply_pause_state(bool)` driven by the epoch |
| `on_step_over` disassembles | `session.step_over()` |
| `on_run_to_eof/eosl` compute cycles | `session.run_to_end_of_frame()/…scanline()` |
| `refresh_panels` calls `snapshot_raster()` | nothing — backend keeps it current |
| `check_breakpoint_hit` compares `paused()` with `was_paused_` | compares `pause_epoch()` with the last seen epoch; the GH #219 auto-enable branch is unchanged |
| owns `SymbolTable` | takes it from the session; `on_load_map_*` keep the QFileDialog/QMessageBox and call `session.symbols().load_*` |
| `ResumeGuard` + modal | unchanged (the modal is Qt; the policy is already pure) — reads `session.state_error()` / `state_error_generation()` |
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
| **Memory panel**: CPU view vs slot view read/write, SP/VRAM/attr highlight | only INSPW (reads) | `QMP-01..05` incl. the slot-view "physical" read that is really a CPU-map read (`memory_panel.cpp:130-139`) — pin the CURRENT behaviour, and note it as a candidate defect |
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

### 6.3 Mutation checks for the #278 reviewer

Each mutation is applied to the REFACTORED tree, in its own build dir, and
must turn at least one named row red (`feedback_mutation_harness_reports_absence`:
prove the harness sees a known mutation first):

1. `step_over()` in the backend: drop the DJNZ case → `QSO-04`.
2. `run_to_end_of_frame()`: target `FB_HEIGHT-1` without `vblank_top` → DVP-11.
3. Pause epoch: increment on resume as well → `QPE-*` (panels refresh while running).
4. `render_layer(ULA_PRIMARY)`: follow the live bank instead of forcing it → DVP-03.
5. `nextreg_peek` → `read` → DVP-PEEK + `QNR-02` (NR 0x2D latch).
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
| WP1 | Header move: `src/qt/` + 6 include edits + CMake sanity across the build matrix + lint row | WP0 | yes (independent of the backend) |
| WP2 | `DebuggerManager` control verbs onto the backend: attach/detach, run/pause, step into/over/out, run-to, EOF/EOSL, epoch-based edge detection, `apply_pause_state`; delete the disassembly and cycle arithmetic from the manager | backend CAPs for REQ-qt-01..07, 15 | serial — the manager is one file and every panel WP touches its wiring |
| WP3 | Rewind + trace + corruption: `on_step_back`, `on_rewind_to_frame`, window rewind toolbar/menu/dialog, trace menu, `update_actions` inputs | REQ-qt-08..11 | after WP2 |
| WP4a | Register-family panels: CPU, MMU, Stack, Call Stack | REQ-qt-16..18, 24, 25 | yes, with 4b/4c/4d |
| WP4b | Register-file panels: NextREG, Sprites, Copper, Audio | REQ-qt-19..23 | yes |
| WP4c | Breakpoints panel + Disasm gutter/context menu + window Breakpoints menu + Watches panel | REQ-qt-13, 14, 17 | yes |
| WP4d | Video panel: `render_layer` + raster/layer-state/palette accessors; `VideoLayerView` shrinks to a wrapper | REQ-qt-25..28 (the backend's biggest item — NEEDS-PROTOTYPE candidate: move `render_to_image` + `replay_*` verbatim into a Qt-free function first and re-run DVP) | yes |
| WP5 | Memory panel (paint-time reads through `read_memory`, bulk read) | REQ-qt-17 | yes with WP4 |
| WP6 | Symbol table ownership move + `on_load_map_*`; magic bp menu; `MainWindow` forwarding unchanged | REQ-qt-12, 14 | after WP2 |
| WP7 | Remove `core/emulator.h` from every `src/debugger/*.cpp`; final reach-around grep = 0; developer-guide chapter 3.9 + `FEATURES.md` unchanged in substance, paths updated | all | serial, last |

Each WP: own branch + worktree, the triplet + `unit-test-sdl`, independent
review, merge, `make bump-patch` — the standing protocol. WP4a-d and WP5 can
run as four agents; WP2/WP3/WP6/WP7 are serial on the manager.

---

## 8. REQ ledger (to `design-backend`)

Sent as `REQ-qt-<n>: <capability> — <why> — <site>`; answers recorded here.

| REQ | Capability | Status vs backend.md v1 |
|---|---|---|
| 01 | attach()/detach() — `debugger_manager.cpp:92-93,149-150` | served: CAP-SES-01/05, CAP-INS-12 |
| **01b** | per-client `set_live_raster(cid, bool)` — the adapter stays attached; window show/hide toggles the render hint — `:90-99, :152-155` | **ACCEPTED** → CAP-SES-05 per-client, OR'd into the render hint + raster walk |
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
| **15b** | listener contract: the Qt listener records only; UI work deferred to the tick (§4) — no backend change, recorded so the backend does not assume UI-in-callback | note |
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

MAPPED against backend.md v1 + the backend's answers (2026-09-26, to land in
v2): **35 CAP ids used (+3 new: CAP-CTL-14, CAP-INS-19, per-client
CAP-SES-05), 17 declined (§3.3), 0 REQs open, 0 reach-arounds.** All 14
sub-REQs ACCEPTED/CONFIRMED; REQ-qt-28 is NEEDS-PROTOTYPE by agreement
(§3.7). To be re-confirmed as "MAPPED" against v2 when broadcast (additions
only expected).

---

## 9. Open questions for the owner

1. **Memory panel "slot view" reads through the CPU map** (`memory_panel.cpp:130-139`):
   selecting "Slot 3 (page 0A)" shows CPU addresses `0x6000-0x7FFF`, which IS
   that page — but only because the slot is currently mapped there; it is not
   a physical-page viewer. Pin as-is for #278 (identity), and decide separately
   whether it should become a physical-page read (the DSL/DZRP will want
   physical reads anyway — `CMD_READ_MEM` is logical, `CMD_WRITE_BANK` is
   physical).
2. **Should a REMOTE client's pause open the local debugger window?** Today
   any pause the GUI did not cause opens it (GH #219 branch,
   `debugger_manager.cpp:691-693`), and under the backend a DZRP/ZRCP/GDB
   pause arrives by the same path. design-dzrp filed this; if the answer is
   "no", the adapter skips auto-enable when `Paused{by}` is a remote client.
   Default proposed: yes, open it (a human at the GUI sees what stopped the
   machine).

Settled as design, not owner questions (moved out of this list):
* script/remote-created subscriptions ARE listed in the Breakpoints panel,
  marked by owner, read-only (REQ-qt-13d confirmed) — DSL/remote PRs, not #278;
* design-dsl's "Script" tab and Alt+1..8 host keys are post-#278 (CAP-EVT
  `Host` names them `script1..8`; the binding is a GH #1 keymap addition —
  Alt+digit is a legal debugger binding, `validate_combo` refuses Alt+LETTER
  only, so the accept-with-warning rule of `GH1-DEBUGGER-KEYMAP-DESIGN.md`
  §4b applies; the emulator window's Alt compounds are E/G/C only,
  `main_window.cpp:2181-2183`);
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
