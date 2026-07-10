# FUSE Z80 opcode test runner (standalone, no GoogleTest required)
add_executable(fuse_z80_test fuse/fuse_z80_test.cpp)
target_link_libraries(fuse_z80_test PRIVATE jnext_cpu)
target_include_directories(fuse_z80_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Stage FUSE test data; configure_file() registers each source as a
# configure-time dep so edits re-copy on next build (file(COPY) doesn't).
file(GLOB FUSE_TEST_DATA "${CMAKE_CURRENT_SOURCE_DIR}/fuse/tests.*")
foreach(_src ${FUSE_TEST_DATA})
    get_filename_component(_fname ${_src} NAME)
    configure_file(${_src} ${CMAKE_CURRENT_BINARY_DIR}/fuse/${_fname} COPYONLY)
endforeach()

# Register as CTest target
add_test(NAME fuse_z80_tests COMMAND fuse_z80_test ${CMAKE_CURRENT_BINARY_DIR}/fuse)

# Z80Cpu /INT pulse-window machine-timing test (Task 2 CPU fix).
# VHDL zxnext.vhd:2033 — pulse width 32 (48K/+3) vs 36 (128K/Pent/Next).
# Standalone, no GoogleTest; jnext_cpu only (matches fuse_z80_test linkage).
add_executable(cpu_int_pulse_test cpu/int_pulse_test.cpp)
target_link_libraries(cpu_int_pulse_test PRIVATE jnext_cpu)
target_include_directories(cpu_int_pulse_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME cpu_int_pulse_tests COMMAND cpu_int_pulse_test)

# CPU/Z80N/IM2 regression test (Task 2 verify Pass-1..Pass-10 retroactive
# coverage). One test per fix landed in commits 65b5918..c526aa4. Pinned
# against VHDL t80n*.vhd, im2_*.vhd, zxnext.vhd.
#
# REQUEST-CHANGES follow-up (2026-05-10): contention-stretch tests for
# Pass-5 / Pass-7 / Pass-9 require ContentionModel + a real Mmu fixture
# so `mem_active_page_for(addr)` resolves to a contended bank; this
# brings in jnext_memory (Mmu / Ram / Rom / ContentionModel). The CPU-
# only sibling tests (FUSE, int_pulse) keep the narrower jnext_cpu link.
add_executable(cpu_z80n_im2_regressions_test cpu/cpu_z80n_im2_regressions_test.cpp)
target_link_libraries(cpu_z80n_im2_regressions_test PRIVATE
    jnext_cpu jnext_memory jnext_peripheral jnext_debug)
target_include_directories(cpu_z80n_im2_regressions_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME cpu_z80n_im2_regressions_tests COMMAND cpu_z80n_im2_regressions_test)

# Rewind / backwards execution tests (standalone, no GoogleTest required)
add_executable(rewind_test rewind/rewind_test.cpp)
target_link_libraries(rewind_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(rewind_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME rewind_tests COMMAND rewind_test)

# Z80N extended opcode test runner
add_executable(z80n_test z80n/z80n_test.cpp)
target_link_libraries(z80n_test PRIVATE jnext_cpu)
target_include_directories(z80n_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
# Stage Z80N test data; configure_file() registers each source as a
# configure-time dep so edits re-copy on next build (file(COPY) doesn't).
file(GLOB Z80N_TEST_DATA "${CMAKE_CURRENT_SOURCE_DIR}/z80n/tests.*")
foreach(_src ${Z80N_TEST_DATA})
    get_filename_component(_fname ${_src} NAME)
    configure_file(${_src} ${CMAKE_CURRENT_BINARY_DIR}/z80n/${_fname} COPYONLY)
endforeach()
add_test(NAME z80n_tests COMMAND z80n_test ${CMAKE_CURRENT_BINARY_DIR}/z80n)

# Copper coprocessor compliance test
add_executable(copper_test copper/copper_test.cpp)
target_link_libraries(copper_test PRIVATE jnext_peripheral jnext_port)
target_include_directories(copper_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME copper_tests COMMAND copper_test)

# Copper integration test (G117 cycle-accurate scheduler verification).
# Drives a real Emulator, programs the Copper through NR 0x60-0x63, and
# observes that 16 MOVEs in one burst all fire within ~3 CPU instructions.
# Mirrors the nextreg_integration_test linkage (full subsystem stack).
add_executable(copper_integration_test copper/copper_integration_test.cpp)
target_link_libraries(copper_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(copper_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME copper_integration_tests COMMAND copper_integration_test)

# Memory/MMU compliance test
add_executable(mmu_test mmu/mmu_test.cpp)
target_link_libraries(mmu_test PRIVATE jnext_memory jnext_peripheral jnext_debug)
target_include_directories(mmu_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME mmu_tests COMMAND mmu_test)

# MMU integration test (G143 re-home: port 0xEFF7 / NR 0x85 b2 gate).
# Hosts plan rows whose observable lives at the port-dispatch tier above
# the bare Mmu class (gates that VHDL applies before forwarding to the
# Mmu module). Mirrors the nextreg_integration_test linkage.
add_executable(mmu_integration_test mmu/mmu_integration_test.cpp)
target_link_libraries(mmu_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(mmu_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME mmu_integration_tests COMMAND mmu_integration_test)

# NextREG register file compliance test (bare NextReg class)
add_executable(nextreg_test nextreg/nextreg_test.cpp)
target_link_libraries(nextreg_test PRIVATE jnext_port)
target_include_directories(nextreg_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME nextreg_tests COMMAND nextreg_test)

# NextREG integration test (full-machine reset defaults via port path)
# Links the full emulator because RST-xx defaults are owned by subsystem
# classes, not the bare NextReg register file. See item 7 in
# .prompts/2026-04-17.md and memory/project_nextreg_integration_test.md.
add_executable(nextreg_integration_test nextreg/nextreg_integration_test.cpp)
target_link_libraries(nextreg_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(nextreg_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME nextreg_integration_tests COMMAND nextreg_integration_test)

# Input subsystem compliance test
# 2026-04-21 (Phase 2 Agent I): NMI-01..07 use a full headless Emulator
# to write NR 0x06 bits 3/4 through the real port path and read back the
# combinational nmi_assert_mf / nmi_assert_divmmc gates. This requires
# the same full link line as nextreg_integration_test / port_test.
add_executable(input_test input/input_test.cpp)
target_link_libraries(input_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(input_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME input_tests COMMAND input_test)

# Input subsystem integration test (Phase 3, 2026-04-21)
# Hosts the 7 plan rows re-homed from input_test.cpp that test the full
# port 0xFE byte assembly path (KBD-22/23, FE-01..05). These rows need
# the port-dispatch wrapper at zxnext.vhd:3459 — the '1' & EAR & '1' &
# i_KBD_COL composition that lives one level above Keyboard::read_rows
# in the port-0xFE handler. Mirrors the ctc_interrupts_test linkage below.
add_executable(input_integration_test input/input_integration_test.cpp)
target_link_libraries(input_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(input_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME input_integration_tests COMMAND input_integration_test)

# Phantom Typist compliance test (Task 19, instant TAP loading).
# Exercises src/input/phantom_typist.{h,cpp} — the FUSE-style
# "wait for ROM to be ready" auto-typer. Pure unit test against the
# PhantomTypist class; no Emulator surface needed. Uses Keyboard
# (jnext_input) as a sink to verify queue side effects.
add_executable(phantom_typist_test phantom_typist/phantom_typist_test.cpp)
target_link_libraries(phantom_typist_test PRIVATE
    jnext_input jnext_memory spdlog::spdlog)
target_include_directories(phantom_typist_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME phantom_typist_tests COMMAND phantom_typist_test)

# CTC compliance test
add_executable(ctc_test ctc/ctc_test.cpp)
target_link_libraries(ctc_test PRIVATE jnext_cpu jnext_peripheral jnext_port)
target_include_directories(ctc_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ctc_tests COMMAND ctc_test)

# CTC + Interrupt Controller integration test (Phase 3c, 2026-04-21)
# Hosts the 10 plan rows re-homed from ctc_test.cpp that require a full
# Emulator + NextReg + Im2Controller integration surface (ULA-INT-01/02/
# 03/05, NR-C0-04, NR-C4-02/03, NR-C6-02, ISC-09, ISC-10). Mirrors the
# nextreg_integration_test linkage above.
add_executable(ctc_interrupts_test ctc_interrupts/ctc_interrupts_test.cpp)
target_link_libraries(ctc_interrupts_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(ctc_interrupts_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ctc_interrupts_tests COMMAND ctc_interrupts_test)

# Layer 2 compliance test
add_executable(layer2_test layer2/layer2_test.cpp)
target_link_libraries(layer2_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(layer2_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME layer2_tests COMMAND layer2_test)

# UART + I2C/RTC compliance test
add_executable(uart_test uart/uart_test.cpp)
target_link_libraries(uart_test PRIVATE jnext_peripheral)
target_include_directories(uart_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME uart_tests COMMAND uart_test)

# UART + I2C integration test (Phase 2 Waves C + D, 2026-04-24)
# Hosts the 12 plan rows re-homed from uart_test.cpp that need a full
# Emulator + NextReg + Im2Controller + port-dispatch surface:
#   INT-01..06 (UART IM2 interrupt vectors via NR 0xC6/0xCA),
#   GATE-01..03 (NR 0x83 port-enable gates for I2C/UART),
#   I2C-10     (I2C port-gate detail row),
#   DUAL-05/06 (dual-channel dispatch + NR 0x0B iomode UART RX multiplex
#               in src/core/emulator.cpp above the per-channel Uart
#               accessors that uart_test.cpp pins).
# Mirrors the ctc_interrupts_test / ula_integration_test / input_integration_test linkage.
add_executable(uart_integration_test uart/uart_integration_test.cpp)
target_link_libraries(uart_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(uart_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME uart_integration_tests COMMAND uart_integration_test)

# Multiface core class compliance test (Wave 1 B1, Task 8 plan, 2026-05-04)
# Pins src/peripheral/multiface.{h,cpp} against multiface.vhd. Bare-class
# tests (FFs, mode dispatch, port_io_dly edge detector, save/load round
# trip) plus Wave 1 B2 (2026-05-04) MF-PORT-* rows that drive the real
# Emulator port dispatcher — the latter pulls in jnext_core transitively,
# so we link the full subsystem set (mirrors nmi_test's recipe).
add_executable(multiface_test multiface/multiface_test.cpp)
target_link_libraries(multiface_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(multiface_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME multiface_tests COMMAND multiface_test)

# DivMMC + SPI compliance test
add_executable(divmmc_test divmmc/divmmc_test.cpp)
# jnext_memory + jnext_debug added 2026-04-21: PRI-01/02/04 need a
# Mmu+DivMmc integration fixture (re-homed from test/mmu/mmu_test.cpp
# commit 7be3deb), which pulls Mmu/Ram/Rom and DebugState transitively.
# jnext_port added 2026-04-28 (Task 8 t1): NA-04..NA-08 wire NR 0x0A /
# 0x83 / 0x06 verification through a NextReg fixture (G123/G124/G125/G131).
# jnext_cpu added 2026-05-03 (G46(a)): DM-RETN-PROPER-01/02 drive
# DivMmc::on_m1_retn_delay() through a real Im2Controller FSM so the
# canonical-ED-45 vs alias-byte distinction is verified end-to-end.
target_link_libraries(divmmc_test PRIVATE jnext_peripheral jnext_memory jnext_debug jnext_port jnext_cpu)
target_include_directories(divmmc_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME divmmc_tests COMMAND divmmc_test)

# SD card SPI state machine compliance test
add_executable(sdcard_test sdcard/sdcard_test.cpp)
target_link_libraries(sdcard_test PRIVATE jnext_peripheral)
target_include_directories(sdcard_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sdcard_tests COMMAND sdcard_test)

# SD ROM extractor compliance test (Task 8 Wave 0.2, 2026-05-04)
# Host-side FAT32 reader for ROM-from-SD extraction at Emulator::init time.
# Pure host code (no Z80 / no peripherals) — links jnext_core only.
# Test fixture is roms/nextzxos-1gb-fat32fix.img (canonical NextZXOS image);
# the test runs from the source-tree root so the relative path resolves.
add_executable(sd_rom_extractor_test sd_rom_extractor/sd_rom_extractor_test.cpp)
target_link_libraries(sd_rom_extractor_test PRIVATE jnext_core)
target_include_directories(sd_rom_extractor_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sd_rom_extractor_tests
         COMMAND sd_rom_extractor_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# FAT32 image read/write + geometry test (Task 27)
# Native reimplementation of the mtools-based SD-image FAT32 cluster fix.
# Pure host code; links jnext_core for extract_sd_rom() (SFN-preservation test).
add_executable(fat32_image_test fat32_image/fat32_image_test.cpp)
target_link_libraries(fat32_image_test PRIVATE jnext_core)
target_include_directories(fat32_image_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME fat32_image_tests COMMAND fat32_image_test)

# SD-card provisioner test (Task 27): path resolution, precedence, download
# seam behaviour (stubbed — never touches the network), and zip extraction.
add_executable(sdcard_provisioner_test sdcard_provisioner/sdcard_provisioner_test.cpp)
target_link_libraries(sdcard_provisioner_test PRIVATE jnext_core ZLIB::ZLIB)
target_include_directories(sdcard_provisioner_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sdcard_provisioner_tests COMMAND sdcard_provisioner_test)

# Sprites subsystem compliance test
add_executable(sprites_test sprites/sprites_test.cpp)
target_link_libraries(sprites_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(sprites_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sprites_tests COMMAND sprites_test)

# Compositor compliance test
add_executable(compositor_test compositor/compositor_test.cpp)
target_link_libraries(compositor_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(compositor_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME compositor_tests COMMAND compositor_test)

# Compositor subsystem integration test (2026-04-24)
# Hosts UDIS-01/02 re-homed from compositor_test §UDIS — NR 0x68 bit 7
# ULA-disable wiring end-to-end (direct write + mid-frame Copper MOVE).
# Needs the full Emulator + CPU + Copper + run_frame loop; link line
# mirrors ula_integration_test.
add_executable(compositor_integration_test compositor/compositor_integration_test.cpp)
target_link_libraries(compositor_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(compositor_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME compositor_integration_tests COMMAND compositor_integration_test)

# ULA video compliance test
add_executable(ula_test ula/ula_test.cpp)
target_link_libraries(ula_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(ula_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ula_tests COMMAND ula_test)

# ULA video subsystem integration test (Phase 3, 2026-04-23)
# Hosts the ~6-8 plan rows that exercise NR 0x26/0x27/0x42/0x43/0x68 and
# port 0xBF3B/0xFF3B/0xFF end-to-end through the real NR-dispatch and
# port-dispatch handlers in src/core/emulator.cpp (the level above the
# narrow Ula accessors that ula_test.cpp pins). Mirrors the linkage of
# nextreg_integration_test / input_integration_test / ctc_interrupts_test.
add_executable(ula_integration_test ula/ula_integration_test.cpp)
target_link_libraries(ula_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(ula_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ula_integration_tests COMMAND ula_integration_test)

# Floating Bus compliance test (Branch C helpers + scaffolding, 2026-04-25)
# Hosts the 26 rows in doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md — 5
# re-homed from the ULA Phase-4 closure + 21 VHDL-justified neighbours
# for port 0xFF (all machines) and port 0x0FFD (+3 only). The 26 plan
# rows still skip() pending Phase 0 audit; Branch C added Phase 3
# fixture helpers (set_raster_position, cpu_in_a_FF, cpu_in_a_0FFD,
# read_port_default) that need the full Emulator surface, so the link
# line now matches ctc_interrupts_test / ula_integration_test.
add_executable(floating_bus_test floating_bus/floating_bus_test.cpp)
target_link_libraries(floating_bus_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(floating_bus_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME floating_bus_tests COMMAND floating_bus_test)

# VideoTiming expansion compliance test
# Hosts the 22 plan rows in doc/testing/VIDEOTIMING-TEST-PLAN-DESIGN.md.
# All 22 rows live post V1 vc_max_/hc_max_ rebase + per-machine
# accessors (display_origin, ula_prefetch_origin_hc, int_position) +
# 60 Hz toggle (2026-04-25, Branches A/B/C).
add_executable(videotiming_test videotiming/videotiming_test.cpp)
# G163 (Section 8) requires driving the full Emulator to exercise the
# mid-frame line-int re-evaluation path — same linkage as
# ctc_interrupts_test / nextreg_integration_test.
target_link_libraries(videotiming_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(videotiming_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME videotiming_tests COMMAND videotiming_test)

# I/O Port Dispatch compliance test
# Links the full emulator because the rewritten plan requires constructing a
# real Emulator (headless) and probing real peripheral handlers, not a
# synthetic clean container. See doc/testing/IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md.
add_executable(port_test port/port_test.cpp)
target_link_libraries(port_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(port_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME port_tests COMMAND port_test)

# Audio subsystem compliance test
add_executable(audio_test audio/audio_test.cpp)
target_link_libraries(audio_test PRIVATE jnext_audio)
target_include_directories(audio_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME audio_tests COMMAND audio_test)

# NMI source pipeline compliance test (Phase 1 scaffold + Wave A, 2026-04-24).
# See doc/design/TASK-NMI-SOURCE-PIPELINE-PLAN.md + doc/testing/
# NMI-PIPELINE-TEST-PLAN-DESIGN.md. Phase 1 carries RST-01; Wave A adds
# six NR02-* rows plus an Emulator integration row that writes NR 0x02
# via the real port path — so the link line widens to the full Emulator
# surface (mirrors uart_integration_test / ula_integration_test).
add_executable(nmi_test nmi/nmi_test.cpp)
target_link_libraries(nmi_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(nmi_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME nmi_tests COMMAND nmi_test)

# NMI source pipeline integration test (Phase 3, 2026-04-24).
# End-to-end rows proving the button / software-NMI chain composes
# correctly inside Emulator::execute_single_instruction: NmiSource
# FSM → /NMI edge → Z80 request_nmi → PC=0x0066 → DivMmc automap
# → RETN clears latches. Mirrors the uart_integration_test /
# ula_integration_test linkage.
add_executable(nmi_integration_test nmi/nmi_integration_test.cpp)
target_link_libraries(nmi_integration_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(nmi_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME nmi_integration_tests COMMAND nmi_integration_test)

# Audio + NextREG integration test (Phase 2 Wave C, 2026-04-24)
# Hosts the 25 plan rows re-homed from audio_test.cpp that need a full
# Emulator + NextReg surface: NR 0x06 bits[1:0]/bit6 (PSG mode + speaker_beep),
# NR 0x08 bits 5/4/3/1/0 (stereo, speaker_en, dac_en, turbosound_en, issue2),
# NR 0x09 bits 7:5 (per-PSG mono), NR 0x2C/0x2D/0x2E (Soundrive mirrors),
# BP-10..13 beep composites, MX-03/20/22 exc_i gating, SD-17/IO-10 dac gate.
# Mirrors the uart_integration_test / ula_integration_test linkage.
add_executable(audio_nextreg_test audio/audio_nextreg_test.cpp)
target_link_libraries(audio_nextreg_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(audio_nextreg_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME audio_nextreg_tests COMMAND audio_nextreg_test)

# Audio + port-dispatch integration test (Phase 2 Wave B, 2026-04-24)
# Hosts the 17 plan rows re-homed from audio_test.cpp that need a full
# Emulator + NextReg + port-dispatch surface:
#   SD-10..16 + SD-18 (DAC port decoding fan-in/out),
#   BP-01, BP-06    (port 0xFE dispatch into beeper/border),
#   IO-01..05       (AY FFFD/BFFD decoding + unmodelled reg-query/+3 aliases),
#   IO-11, IO-12    (port-to-channel fan-in + AY vs DAC conflict guard).
# Mirrors the uart_integration_test / ula_integration_test linkage.
add_executable(audio_port_dispatch_test audio/audio_port_dispatch_test.cpp)
target_link_libraries(audio_port_dispatch_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(audio_port_dispatch_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME audio_port_dispatch_tests COMMAND audio_port_dispatch_test)

# DMA subsystem compliance test
add_executable(dma_test dma/dma_test.cpp)
target_link_libraries(dma_test PRIVATE jnext_peripheral jnext_memory jnext_port)
target_include_directories(dma_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME dma_tests COMMAND dma_test)

# Tilemap subsystem compliance test
add_executable(tilemap_test tilemap/tilemap_test.cpp)
target_link_libraries(tilemap_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(tilemap_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tilemap_tests COMMAND tilemap_test)

# Contention Model compliance test (2026-04-24)
# Scaffolding commit: all 68 plan rows start as skip() pending the
# Phase A VHDL audit of ContentionModel. See
# doc/testing/CONTENTION-TEST-PLAN-DESIGN.md. Harness-only today; the
# bare-class tests (§4-§7, §12 Phase A) construct ContentionModel
# directly (jnext_memory), and the full-Emulator rows (§8 B, §9-§11,
# §13-§14) will link the full emulator stack at un-skip time. For the
# scaffolding commit we link the same full set up-front so the binary
# is ready for either tier without a CMake churn.
add_executable(contention_test contention/contention_test.cpp)
target_link_libraries(contention_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(contention_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME contention_tests COMMAND contention_test)

# Task 21 — CPU profiler unit test (Profiler class).
add_executable(profiler_test profiler/profiler_test.cpp)
target_link_libraries(profiler_test PRIVATE
    jnext_profiler jnext_memory jnext_peripheral jnext_port jnext_debug)
target_include_directories(profiler_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME profiler_tests COMMAND profiler_test)

# GoogleTest-based tests (if available)
find_package(GTest QUIET)
if(GTest_FOUND)
    file(GLOB_RECURSE GTEST_SOURCES "test_*.cpp")
    if(GTEST_SOURCES)
        add_executable(jnext_tests ${GTEST_SOURCES})
        target_link_libraries(jnext_tests PRIVATE GTest::gtest_main
            jnext_core jnext_cpu jnext_memory jnext_video
            jnext_audio jnext_port jnext_peripheral jnext_input jnext_save)
        target_include_directories(jnext_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
        include(GoogleTest)
        gtest_discover_tests(jnext_tests)
    endif()
endif()
