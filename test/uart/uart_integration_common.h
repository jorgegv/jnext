// Shared scaffolding of the two UART integration suites (GH #214):
// uart_integration_test (every OS) and uart_posix_test (`# os: posix`). The
// helpers, fixtures and fakes below were the part of uart_integration_test.cpp
// that is not a test row; they moved here verbatim so the POSIX-only rows
// (FIFO and pty cables, the NextPi link, QEMU) can live in a suite of their own
// without copying them. Each suite includes this header into its own translation
// unit, so the counters and result lists are per suite.
#pragma once

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/pi_qemu.h"
#include "core/nextpi_provisioner.h"
#include "core/rzx.h"
#include "debug/debug_state.h"
#include "debug/rewind_buffer.h"
#include "peripheral/joy_uart_link.h"
#include "peripheral/pi_uart_device.h"
#include "peripheral/joy_uart_source.h"
#include "peripheral/uart_device.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <zlib.h>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#include <process.h>
#endif
#include "../row_id.h"
#include "../test_portable.h"

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    Result r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void skip(const char* id, const char* reason) {
    report_row_id(id);
    g_skipped.push_back({id, reason});
}

// printf-style detail formatter for check() callers that need runtime values.
static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

std::string hex2(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
}

std::string detail_eq(uint8_t got, uint8_t expected) {
    return "got=" + hex2(got) + " expected=" + hex2(expected);
}

// Space-separated hex render of a byte stream, for JOY-* failure details.
std::string bytes_hex(const std::vector<uint8_t>& bytes) {
    std::string out;
    for (uint8_t b : bytes) {
        if (!out.empty()) out += ' ';
        out += hex2(b);
    }
    return out;
}

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Fresh-state idiom: re-initialise the emulator before each scenario so
// cross-test state (scheduler queues, latched interrupt status, etc.)
// cannot leak between scopes.
static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Read NextREG register through the real port path (OUT 0x243B,reg;
// IN 0x253B). Mirrors the idiom used by ctc_interrupts_test.cpp.
static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

// Write NextREG register through the real port path.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// Latch any pending Im2 int_req edges (inject_rx / on_tx_empty call
// raise_req(); the wrapper edge detect runs inside Im2Controller::tick()
// which is driven by Emulator::run_frame in the normal flow).
//
// run_frame() also ticks the UART (master_cycles per instruction), which
// drains TX-FIFO bytes through byte_transfer_ticks() (~2430 cycles at the
// default 115200-baud/28MHz prescaler). A single full frame is ~567k
// master cycles — comfortably more than enough to drain a few-byte TX
// FIFO and fire `on_tx_empty`.
static void settle(Emulator& emu) {
    emu.run_frame();
}

// ══════════════════════════════════════════════════════════════════════
// Section INT — UART IM2 interrupt vectors (INT-01..06)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:1930-1944, :1949-1950:
//   Priority slot 1 = UART0 RX : uart0_rx_near_full OR (uart0_rx_avail AND NOT nr_c6(1))
//                     int_en   : nr_c6(1) OR nr_c6(0)
//   Priority slot 2 = UART1 RX : uart1_rx_near_full OR (uart1_rx_avail AND NOT nr_c6(5))
//                     int_en   : nr_c6(5) OR nr_c6(4)
//   Priority slot 12= UART0 TX : uart0_tx_empty
//                     int_en   : nr_c6(2)
//   Priority slot 13= UART1 TX : uart1_tx_empty
//                     int_en   : nr_c6(6)
//
// Observable: after ticking, NR 0xCA packs UART int_status per
// zxnext.vhd:6253-6254 and src/cpu/im2.cpp:313-323:
//   bit 6 = UART1_TX, bits 5:4 = UART1_RX (duplicated), bit 2 = UART0_TX,
//   bits 1:0 = UART0_RX (duplicated), bits 7/3 = literal 0.
// ══════════════════════════════════════════════════════════════════════


// ══════════════════════════════════════════════════════════════════════
// Section GATE — Port-enable gates (GATE-01..03 + I2C-10)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:2392-2420, :5499-5509:
//   internal_port_enable[31:0] = NR 0x85 : NR 0x84 : NR 0x83 : NR 0x82
//                                (bits 31:24 : 23:16 : 15:8  : 7:0)
//   bit 8  (NR 0x83 bit 0) → port_divmmc_io_en
//   bit 10 (NR 0x83 bit 2) → port_i2c_io_en   (0x103B / 0x113B)
//   bit 12 (NR 0x83 bit 4) → port_uart_io_en  (0x133B-0x163B)
//
// NOTE: the RE-HOME comments in test/uart/uart_test.cpp (lines 1294-1305)
// and the plan doc (§Cluster 6, 7) cite "NR 0x82 bit 4" / "NR 0x82 bit 2".
// Per VHDL:2392 the low byte of internal_port_enable comes from NR 0x82
// and the NEXT byte (bits 8..15) from NR 0x83. Bits 10 and 12 therefore
// map to NR 0x83 — confirmed by the DivMMC gate at emulator.cpp:1467-1476
// which also uses NR 0x83 (bit 0 = bit 8 = port_divmmc_io_en). Plan-doc
// text is imprecise; this suite cites the VHDL-correct mapping.
//
// At reset, NR 0x82/0x83/0x84 load 0xFF (zxnext.vhd:5052-5057 via
// src/port/nextreg.cpp:38-45), so the gates are open by default and
// existing tests that hit these ports do not regress.
// ══════════════════════════════════════════════════════════════════════


// ══════════════════════════════════════════════════════════════════════
// Section I2C — I2C port-enable gate detail row (I2C-10)
// ══════════════════════════════════════════════════════════════════════


// ══════════════════════════════════════════════════════════════════════
// Wave D rows — DUAL-05/06 (dual-UART routing + joystick IO-mode mux).
// ══════════════════════════════════════════════════════════════════════

// Drive the full UART advance so pending TX bytes complete. The
// default Next config runs UART 0 at 115200 @ 28 MHz → ~243 * 10 =
// 2430 master cycles for one 8N1 byte. Tick generously to guarantee
// completion of a single byte regardless of framing changes.
static void tick_uart_byte(Emulator& emu) {
    emu.uart().tick(8000);
}




// ══════════════════════════════════════════════════════════════════════
// Section DEV — UartDevice attach/detach seam (DEV-01..04)
// ══════════════════════════════════════════════════════════════════════
//
// Pins src/peripheral/uart_device.h + Uart::attach_device/detach_device.
// The seam is the backend socket for the ESP-01 on UART 0 (zxnext.vhd:1611,
// :3381 — UART 0 is the ESP, UART 1 the Pi header) and must be behaviour-
// preserving when nothing is attached: an unattached channel keeps looping
// TX back into its own RX FIFO, which is what the 116 pre-existing UART rows
// observe.
//
// Every row drives the guest side through the REAL port path (OUT 0x153B to
// select the channel, OUT 0x133B to transmit, IN 0x143B to receive) so the
// seam is exercised exactly as a Z80 program would reach it.
//
// Lifetime discipline in this file: the stubs are stack-allocated and `emu`
// outlives every scope, so each scope MUST detach before its stub dies —
// a leaked attachment would leave a dangling UartDevice* for later rows.
// ══════════════════════════════════════════════════════════════════════

namespace {

// Minimal UartDevice backend: records everything the guest sends, and can
// push bytes the other way on demand. `send_to_guest` is protected on the
// base class, so this stub also proves the intended subclass access path.
class StubUartDevice : public UartDevice {
public:
    void receive(uint8_t byte) override { rx.push_back(byte); }
    void poll() override { ++polls; }

    /// Push one byte toward the guest (models a socket delivering data).
    void send(uint8_t byte) { send_to_guest(byte); }

    std::vector<uint8_t> rx;
    int                  polls = 0;
};

} // namespace


// ══════════════════════════════════════════════════════════════════════
// Section ESP — the REAL emulated ESP-01 on UART 0 (ESP-01..04)
// ══════════════════════════════════════════════════════════════════════
//
// These four IDs sat `missing`/`missing` in the traceability matrix and as
// deliberate `// WONT` comments in test/uart/uart_test.cpp, from the era when
// jnext had no ESP at all ("a self-contained networking feature nobody had
// built"). GH #25 built it, so the revisit-trigger fired.
//
// They are NOT duplicates of DEV-01..05 above. Those drive the hand-written
// `StubUartDevice`, which proves the SEAM. A stub cannot tell you whether the
// thing `--esp` actually constructs — a `ThreadedEsp` wrapping a real
// `AtEngine` behind an `EspGatedTransport`, built by `Emulator::setup_esp` —
// is on the wire and answering. Every row below uses that one, reached the
// way a user reaches it.
//
// Each row builds its OWN Emulator rather than using `fresh(emu)`: the ESP is
// constructed from `EmulatorConfig::esp_enabled`, and `setup_esp` deliberately
// builds only once per Emulator (a soft reset must not drop a live TCP
// connection — design doc §4.3), so the shared fixture cannot express both
// arms.
//
// HERMETIC, and not by luck: `AT` and `ATE1` never touch the transport, so
// nothing here resolves a name or opens a socket. The socket half — a real
// connect, `AT+CIPSEND` and `+IPD` framing against a live TCP peer — is the
// `esp-loopback-func` regression row.

namespace {

/// Bytes rendered with CR/LF as escapes, so a failing byte-stream row reads
/// on one line and a stray terminator cannot hide in the output.
std::string visible(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\r')      out += "\\r";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

EmulatorConfig esp_config(bool enabled) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.esp_enabled = enabled;
    return cfg;
}

void uart0_send(Emulator& emu, const std::string& line) {
    emu.port().out(0x153B, 0x00);           // select channel 0 (the ESP)
    for (char c : line) emu.port().out(0x133B, static_cast<uint8_t>(c));
}

/// What a drain saw, and how long it had to look. The frame/millisecond
/// figures exist so a FAILING row can say whether it gave up after the full
/// wait — a genuinely broken egress path — rather than leaving a reader to
/// guess (GH #186).
struct Drained {
    std::string bytes;
    int         frames = 0;
    double      ms     = 0.0;
};

/// Settle window, in EMULATED frames, kept after the wanted bytes appear.
///
/// This is the ORIGINAL fixed budget, and it stays fixed, because the job it
/// does is an emulated-time one: the reply is paced onto the wire at baud
/// (2430 master cycles per byte at the 115200 reset default) and the adapter's
/// hot-path tick gate is only re-evaluated in the once-per-frame `poll()`, so
/// bytes keep dribbling in for a frame or two after the first one lands.
/// Holding the window open for this many frames PAST the match is what keeps
/// the rows able to catch a reply with unwanted bytes trailing it — the
/// property the old fixed budget got for free, and the one a naive
/// stop-on-match retry would have silently dropped.
constexpr int ESP_SETTLE_FRAMES = 4;

/// Upper bound on the wait, in HOST milliseconds — the unit that matters,
/// because the thing being waited for is a HOST THREAD (GH #186).
///
/// The ESP runs on `esp::ThreadedEsp`'s worker: the guest's TX bytes go into
/// an inbound queue and only that thread parses them and queues the answer.
/// The row's old budget was `ESP_SETTLE_FRAMES` of emulated time, which
/// headless at max speed is ~5 ms of WALL CLOCK — measured 1.3 ms/frame — so
/// the row silently required the host to schedule a freshly-woken thread
/// inside 5 ms. Measured cliff: injecting a worker wake latency of 6 ms
/// reproduces "RX drained 0 bytes" exactly; 4 ms does not. Six milliseconds is
/// an ordinary CFS wake latency on a loaded box, which is why this failed
/// three times overnight on branches that touch neither the ESP nor the UART.
///
/// The wait is bounded and generous — 1000x the measured cliff — and it is NOT
/// a way to let a broken path pass: the stop condition is that the drained
/// bytes EQUAL what the row wants, so a loopback (`AT\r\n` where `\r\nOK\r\n`
/// is wanted) or an absent backend never satisfies it and the row still FAILS,
/// after burning the full wait. Mutation-verified.
constexpr int ESP_WAIT_MS = 5000;

/// Backstop so a pathologically cheap frame cannot spin here forever; at the
/// measured 1.3 ms/frame the millisecond deadline is what actually bites.
constexpr int ESP_MAX_FRAMES = 20000;

/// Harvest everything that reaches the RX FIFO, until `want` has arrived (plus
/// the settle window above), or the bounds give up.
///
/// `want` empty means "nothing is expected": the drain then runs exactly
/// `ESP_SETTLE_FRAMES` frames, which is the original behaviour, because there
/// is nothing to wait FOR and a row that expects silence must not sit for the
/// full timeout to learn it.
///
/// Draining across frames is what a real program does too.
Drained uart0_drain(Emulator& emu, const std::string& want) {
    Drained  d;
    const auto start    = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(ESP_WAIT_MS);
    int matched_at = -1;

    for (int i = 0; i < ESP_MAX_FRAMES; ++i) {
        emu.run_frame();
        emu.port().out(0x153B, 0x00);
        while (emu.port().in(0x133B) & 0x01)          // status b0 = RX available
            d.bytes += static_cast<char>(emu.port().in(0x143B));
        d.frames = i + 1;

        if (matched_at < 0 && !want.empty() && d.bytes == want) matched_at = i;

        const bool settled = (matched_at >= 0)
                                 ? (i - matched_at >= ESP_SETTLE_FRAMES)
                                 : (want.empty() && d.frames >= ESP_SETTLE_FRAMES);
        if (settled) break;
        if (std::chrono::steady_clock::now() >= deadline) break;   // gave up
    }
    d.ms = std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start).count();
    return d;
}

/// The wait as a row's failure text renders it.
std::string waited(const Drained& d) {
    return fmt("after %d frames / %.0f ms", d.frames, d.ms);
}

} // namespace


// ══════════════════════════════════════════════════════════════════════
// Section JOY — the joystick-connector UART cable (JOY-01..12), GH #251, #254
// ══════════════════════════════════════════════════════════════════════
//
// Two things that had no coverage at all before GH #251:
//
//   * The MUX IS EXCLUSIVE. VHDL zxnext.vhd:3340-3346 — when the joystick
//     connector owns a channel, that channel's `*_rx` selects `joy_uart_rx`
//     (so the module's own RX pin is not selected) and its module-facing TX
//     pin is held at '1'. The ESP path in jnext ignored NR 0x0B entirely, so a
//     bench built on `--esp` would have passed with the mux wired arbitrarily
//     wrongly — which is why no bench was built until the routing was
//     enforceable.
//
//   * A HOST SERIAL SOURCE CAN REACH THE PIN. `Emulator::inject_joy_uart_rx`
//     had exactly two callers, both in this file, so nothing outside the test
//     tree could put a byte on the joystick UART — the whole of the issue.
//
// The rows drive the guest side through the real port path and read the source
// side through the real `--joy-uart-rx` config field, file and all.
// ══════════════════════════════════════════════════════════════════════

namespace {

// A tag unique to this process: concurrent runs share the temp directory, and a
// fixed name lets one run replace or delete another's file (or FIFO).
std::string pid_tag() {
#ifdef _WIN32
    return std::to_string(::_getpid());
#else
    return std::to_string(::getpid());
#endif
}

// A `--joy-uart-rx` stream on disk, removed when the row's scope ends. Real
// file I/O rather than a test-only injection hook, so the row covers
// read_joy_uart_source_file() and the config seam a command line actually uses.
class TempSourceFile {
public:
    explicit TempSourceFile(const std::string& tag, const std::vector<uint8_t>& bytes) {
        path_ = (std::filesystem::temp_directory_path()
                 / ("jnext-joy-uart-" + tag + "-" + pid_tag() + ".bin")).string();
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    ~TempSourceFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempSourceFile(const TempSourceFile&)            = delete;
    TempSourceFile& operator=(const TempSourceFile&) = delete;

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// One step of a scheduled cable run: the cursor as it stands at the frame
// START, then the frame, then every byte the guest actually read out of
// channel 0 during it. Compared whole, so a replay that merely lands on the
// right counters while delivering different BYTES is still a failure.
struct CableFrame {
    std::size_t          delivered = 0;
    std::size_t          dropped   = 0;
    std::vector<uint8_t> got;

    bool operator==(const CableFrame& o) const {
        return delivered == o.delivered && dropped == o.dropped && got == o.got;
    }
};

// Run one step with the mux held at `nr_0b`, draining channel 0 afterwards.
//
// The caller indexes its schedule by its OWN step counter, never by
// `Emulator::frame_num()`: a rewind to step N restores the machine to the top
// of step N but leaves `frame_num()` reading N+1, because run_frame() bumps the
// counter before the snapshot is taken. A schedule keyed off it would therefore
// drift by one frame across the rewind and the replay would no longer be a
// replay of the same run.
CableFrame run_cable_frame(Emulator& emu, uint8_t nr_0b) {
    CableFrame f;
    if (const JoyUartSource* src = emu.joy_uart_source()) {
        f.delivered = src->delivered();
        f.dropped   = src->dropped();
    }
    emu.nextreg().write(0x0B, nr_0b);
    emu.run_frame();
    emu.port().out(0x153B, 0x00);
    while (!emu.uart().channel(0).rx_empty())
        f.got.push_back(emu.port().in(0x143B));
    return f;
}

// Rewind to `frame` and leave the machine RUNNABLE.
//
// `Emulator::rewind_to_frame` deliberately ends PAUSED — a rewind in the GUI
// lands the user in the debugger at the restored instant — so a programmatic
// replay that does not resume silently executes NOTHING and every "the replay
// matched" assertion becomes a comparison of empty frames against empty frames.
// (It used to arm the machine too, by the `active()` bit this also cleared; GH
// #278 WP3 stopped that and WP4c retired the bit.)
bool rewind_and_resume(Emulator& emu, uint32_t frame) {
    if (!emu.rewind_to_frame(frame)) return false;
    emu.debug_state().resume();
    return true;
}

// Index of the first step at which two runs disagree, or -1 if they agree over
// `count` steps starting at `base` in `forward`.
int first_divergent_step(const std::vector<CableFrame>& forward, int base,
                         const std::vector<CableFrame>& replay) {
    for (std::size_t j = 0; j < replay.size(); ++j) {
        const std::size_t f = static_cast<std::size_t>(base) + j;
        if (f >= forward.size() || !(replay[j] == forward[f]))
            return static_cast<int>(base + j);
    }
    return -1;
}

// A stream long enough to outlive the whole run — an exhausted cursor cannot
// move, and every assertion below would pass trivially against one.
std::vector<uint8_t> cable_stream() {
    std::vector<uint8_t> s(8192);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<uint8_t>((i * 7 + 1) & 0xFF);
    return s;
}

// Config for a Next with a joystick serial cable attached.
EmulatorConfig joy_config(const std::string& file, int connector,
                          uint32_t delay_frames) {
    EmulatorConfig cfg;
    cfg.type                     = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames     = 0;
    cfg.joy_uart_rx_file         = file;
    cfg.joy_uart_connector       = connector;
    cfg.joy_uart_rx_delay_frames = delay_frames;
    return cfg;
}

// Run frames with the guest's NR 0x0B held at `nr_0b`, draining `channel`'s RX
// FIFO through the real port path after each one. The NR is re-asserted every
// frame because the boot firmware is running underneath and this row must
// observe the mux it asked for, not whatever the ROM last left behind.
std::vector<uint8_t> run_and_drain(Emulator& emu, uint8_t nr_0b, int channel,
                                   int frames) {
    std::vector<uint8_t> got;
    for (int f = 0; f < frames; ++f) {
        emu.nextreg().write(0x0B, nr_0b);
        emu.run_frame();
        emu.port().out(0x153B, static_cast<uint8_t>(channel ? 0x40 : 0x00));
        while (!emu.uart().channel(channel).rx_empty())
            got.push_back(emu.port().in(0x143B));
    }
    return got;
}


// ── GH #252 live-cable helpers ────────────────────────────────────────
//
// Real descriptors throughout. A mock endpoint would assert that the pacing
// arithmetic is self-consistent and nothing about whether a FIFO can be opened
// without a peer, which is the whole of the defect GH #252 reports.

// A FIFO pair on disk with the host's two ends held open, removed when the
// row's scope ends.
//
// `attach()` is separate from the constructor because the ORDER matters and is
// itself part of what is under test: jnext opens `<base>.rx` for reading at
// Emulator::init(), and only then can the host open the same path for writing
// at all (O_WRONLY on a FIFO with no reader is the ENXIO the issue reports).
class TempFifoCable {
public:
    explicit TempFifoCable(const std::string& tag) {
        base_ = (std::filesystem::temp_directory_path()
                 / ("jnext-joy-cable-" + tag + "-" + pid_tag())).string();
        remove_files();
    }
    ~TempFifoCable() {
        close_host_rx_writer();
        close_host_tx_reader();
        remove_files();
    }
    TempFifoCable(const TempFifoCable&)            = delete;
    TempFifoCable& operator=(const TempFifoCable&) = delete;

    const std::string& base() const { return base_; }

    /// Open the host's ends, AFTER the emulator has created and opened its own.
    /// `open_tx_reader` false leaves `<base>.tx` with no reader, which is the
    /// state a run starts in before the debugger on the host attaches.
    bool attach(bool open_tx_reader = true) {
#ifdef _WIN32
        (void)open_tx_reader;
        return false;
#else
        rx_w_ = ::open((base_ + ".rx").c_str(), O_WRONLY | O_NONBLOCK);
        if (open_tx_reader && !open_tx_reader_now()) return false;
        return rx_w_ >= 0;
#endif
    }

    bool open_tx_reader_now() {
#ifdef _WIN32
        return false;
#else
        tx_r_ = ::open((base_ + ".tx").c_str(), O_RDONLY | O_NONBLOCK);
        return tx_r_ >= 0;
#endif
    }

    void close_host_tx_reader() {
#ifndef _WIN32
        if (tx_r_ >= 0) { ::close(tx_r_); tx_r_ = -1; }
#endif
    }
    void close_host_rx_writer() {
#ifndef _WIN32
        if (rx_w_ >= 0) { ::close(rx_w_); rx_w_ = -1; }
#endif
    }

    /// Host → Next. Returns how many bytes the pipe took.
    std::size_t send(const std::vector<uint8_t>& bytes) {
#ifdef _WIN32
        (void)bytes; return 0;
#else
        if (rx_w_ < 0 || bytes.empty()) return 0;
        const ssize_t n = ::write(rx_w_, bytes.data(), bytes.size());
        return (n > 0) ? static_cast<std::size_t>(n) : 0;
#endif
    }

    /// Next → host, non-blocking; appends whatever is there right now.
    std::vector<uint8_t> drain() {
        std::vector<uint8_t> got;
#ifndef _WIN32
        if (tx_r_ < 0) return got;
        uint8_t buf[512];
        for (;;) {
            const ssize_t n = ::read(tx_r_, buf, sizeof(buf));
            if (n <= 0) break;
            got.insert(got.end(), buf, buf + n);
        }
#endif
        return got;
    }

private:
    void remove_files() {
        std::error_code ec;
        std::filesystem::remove(base_ + ".rx", ec);
        std::filesystem::remove(base_ + ".tx", ec);
    }
    std::string base_;
    int         rx_w_ = -1;
    int         tx_r_ = -1;
};

// Config for a Next with a LIVE joystick cable attached.
EmulatorConfig link_config(const std::string& base, int connector) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.joy_uart_fifo        = base;
    cfg.joy_uart_connector   = connector;
    return cfg;
}

// Transmit one byte from the guest on `channel` with NR 0x0B held at `nr_0b`,
// and run a whole frame so the byte-level TX engine completes it and the
// cable's once-per-frame poll() runs.
void guest_transmit_frame(Emulator& emu, uint8_t nr_0b, int channel, uint8_t byte) {
    emu.nextreg().write(0x0B, nr_0b);
    emu.port().out(0x153B, static_cast<uint8_t>(channel ? 0x40 : 0x00));
    emu.port().out(0x133B, byte);
    emu.run_frame();
}

} // namespace


// ══════════════════════════════════════════════════════════════════════
// The LIVE Raspberry Pi serial link on UART 1 (NextPi, --nextpi)
// ══════════════════════════════════════════════════════════════════════
//
// The wire on the Pi GPIO header handed to the host, as a `UartDevice` on
// UART 1. Real FIFOs, for the joystick rows' reason: the host plumbing is the
// part that fails at `open()`, and only real descriptors can find that.
//
// Every row tolerates a missing link (`emu.pi_uart() == nullptr`) and FAILS on
// it rather than crashing, so deleting the `setup_pi_uart()` call from init()
// turns every row red — the demonstration that the rows test the feature.

namespace {

EmulatorConfig pi_config(const std::string& base) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.pi_uart_fifo_rx      = base + ".rx";
    cfg.pi_uart_fifo_tx      = base + ".tx";
    return cfg;
}

// One frame with NR 0xA0 held at `nr_a0` (NR 0x0B at `nr_0b`) and UART 1
// selected, then drain UART 1's RX FIFO through port 0x143B. NR 0xA0 is
// re-written every frame for the reason the JOY rows re-write NR 0x0B: the
// guest's own ROM runs inside run_frame() and owns the register too.
std::vector<uint8_t> pi_frame(Emulator& emu, uint8_t nr_a0, uint8_t nr_0b = 0x00) {
    emu.nextreg().write(0xA0, nr_a0);
    emu.nextreg().write(0x0B, nr_0b);
    emu.run_frame();
    emu.port().out(0x153B, 0x40);
    std::vector<uint8_t> got;
    while (!emu.uart().channel(1).rx_empty())
        got.push_back(emu.port().in(0x143B));
    return got;
}

// The guest transmits `byte` on UART 1 with the registers held as above, and a
// whole frame runs so the TX engine completes it and the link's poll() flushes.
void pi_transmit_frame(Emulator& emu, uint8_t nr_a0, uint8_t byte, uint8_t nr_0b = 0x00) {
    emu.nextreg().write(0xA0, nr_a0);
    emu.nextreg().write(0x0B, nr_0b);
    emu.port().out(0x153B, 0x40);
    emu.port().out(0x133B, byte);
    emu.run_frame();
}

} // namespace


// ══════════════════════════════════════════════════════════════════════
// NextPi under QEMU, launched by jnext (--nextpi)
// ══════════════════════════════════════════════════════════════════════
//
// No real QEMU and no NextPi image: a shell-script stand-in takes QEMU's
// place. It answers on the `-chardev pipe` exactly as QEMU's pipe chardev does
// — both FIFO ends opened read-write, so neither side ever sees EOF — prints
// "SUP> " like the NextPi Supervisor, and appends whatever the Next sends to a
// file. What is under test is jnext's half: the directory checks, the overlay,
// the FIFOs, the spawn, the wiring into UART 1, and the teardown.

namespace {

#ifndef _WIN32
bool write_script(const std::filesystem::path& path, const std::string& body) {
    std::ofstream f(path);
    f << body;
    f.close();
    return ::chmod(path.c_str(), 0755) == 0;
}
#endif

/// A provisioned NextPi directory's layout, with empty stand-in files, plus
/// a stand-in `qemu-system-arm` and `qemu-img` in `bin/`.
class FakeNextPi {
public:
    explicit FakeNextPi(const std::string& tag) {
        root_ = std::filesystem::temp_directory_path() / ("jnext-fake-nextpi-" + tag + "-" + pid_tag());
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_ / "dist" / "boot", ec);
        std::filesystem::create_directories(root_ / "bin", ec);
        for (const char* f : {"dist/nextpi.img", "dist/boot/kernel.img",
                              "dist/boot/bcm2708-rpi-zero.dtb"})
            std::ofstream(root_ / f).put('\0');
#ifndef _WIN32
        // What the stand-in records — its pid, environment, open descriptors
        // and arguments — is what the rows check about the child jnext made.
        // The pid goes LAST: `child_pid()` returning is the rows' signal that
        // the other records are complete. Written first, a row could read
        // `env`, `fds` or `args` after the redirection had created the file
        // and before the command had filled it.
        const std::string body =
                  "here=$(dirname \"$0\")\n"
                  "env > \"$here/env\"\n"
                  "ls /dev/fd > \"$here/fds\" 2>/dev/null\n"
                  // fd 3 cannot be read off that listing: `ls` opens the
                  // directory itself, as fd 3 when 3 is free. The shell's own
                  // redirection opens nothing.
                  "if ( : <&3 ) 2>/dev/null; then echo open; else echo closed; fi > \"$here/fd3\"\n"
                  "for a in \"$@\"; do case \"$a\" in pipe,*path=*) base=\"${a##*path=}\" ;; esac; done\n"
                  "printf '%s\\n' \"$@\" > \"$here/args\"\n"
                  "echo $$ > \"$here/pid\"\n"
                  "exec 3<>\"$base.in\" 4<>\"$base.out\"\n"
                  "printf 'SUP> ' >&4\n"
                  "exec cat <&3 >> \"$here/received\"\n";
        ok_ = write_script(root_ / "bin" / "qemu-system-arm", "#!/bin/sh\n" + body)
           // A QEMU that ignores SIGTERM (it is inherited across exec).
           && write_script(root_ / "bin" / "qemu-stubborn", "#!/bin/sh\ntrap '' TERM\n" + body)
           && write_script(root_ / "bin" / "qemu-img",
                  "#!/bin/sh\n"
                  "for a in \"$@\"; do overlay=\"$prev\"; prev=\"$a\"; done\n"
                  ": > \"$overlay\"\n")
           && write_script(root_ / "bin" / "qemu-fails", "#!/bin/sh\nexit 3\n")
           // Outlives the start check, then exits on its own with status 7.
           && write_script(root_ / "bin" / "qemu-exits-7", "#!/bin/sh\nsleep 1\nexit 7\n");
#endif
    }
    ~FakeNextPi() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    FakeNextPi(const FakeNextPi&)            = delete;
    FakeNextPi& operator=(const FakeNextPi&) = delete;

    bool ok() const { return ok_; }
    std::string dir() const { return (root_ / "dist").string(); }
    std::string bin(const char* name) const { return (root_ / "bin" / name).string(); }

    /// What the stand-in has received from the Next so far.
    std::string received() const {
        std::ifstream f(root_ / "bin" / "received");
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    /// Poll for `want` to have arrived, up to two seconds of wall clock — the
    /// stand-in is a separate process, so its writes land when they land.
    bool wait_received(const std::string& want) const {
        for (int i = 0; i < 40; ++i) {
            if (received() == want) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
    std::string args() const { return file("args"); }
    /// One of the files the stand-in writes in bin/ ("pid", "env", "fds", ...).
    std::string file(const char* name) const {
        std::ifstream f(root_ / "bin" / name);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    /// The stand-in's own pid, once it has started (0 if it never did).
    int child_pid() const {
        for (int i = 0; i < 100; ++i) {
            const std::string p = file("pid");
            if (!p.empty() && p.back() == '\n') return std::atoi(p.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return 0;
    }

private:
    std::filesystem::path root_;
    bool                  ok_ = false;
};

EmulatorConfig pi_qemu_config(const PiQemu& qemu) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.pi_uart_fifo_rx      = qemu.rx_path();
    cfg.pi_uart_fifo_tx      = qemu.tx_path();
    return cfg;
}

bool process_alive(int pid) {
#ifndef _WIN32
    if (pid <= 0 || ::kill(pid, 0) != 0) return false;
#ifdef __linux__
    // A zombie is not running, but kill(pid, 0) still succeeds on it. The
    // stand-in becomes one when stop() SIGKILLs it together with its watchdog
    // parent: it is re-parented to PID 1, and a container's PID 1 (CI's
    // fedora:44 job) need not reap it. Field 3 of /proc/<pid>/stat is the
    // state; the name before it is parenthesised and may contain spaces.
    // Read with read(2), not a stream: the process may be reaped between the
    // open and the read, which then fails with ESRCH, and libstdc++'s filebuf
    // turns that failure into an exception that escapes istreambuf_iterator.
    std::string stat;
    const int fd = ::open(("/proc/" + std::to_string(pid) + "/stat").c_str(), O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        const ssize_t n = ::read(fd, buf, sizeof buf);
        ::close(fd);
        if (n > 0) stat.assign(buf, static_cast<std::size_t>(n));
    }
    const std::size_t rp = stat.rfind(')');
    if (rp != std::string::npos && rp + 2 < stat.size() && stat[rp + 2] == 'Z') return false;
#endif
    return true;
#else
    (void)pid;
    return false;
#endif
}

bool contains(const std::vector<std::string>& v, const std::string& a, const std::string& b) {
    for (std::size_t i = 0; i + 1 < v.size(); ++i)
        if (v[i] == a && v[i + 1] == b) return true;
    return false;
}

} // namespace


// ══════════════════════════════════════════════════════════════════════
// The NextPi directory, prepared on first use (nextpi_provisioner)
// ══════════════════════════════════════════════════════════════════════
//
// Offline: the "mirror" is a temporary directory and the download seam copies
// from it, as sdcard_provisioner's tests do. The archive is built here — a
// gzip-compressed tar in GNU format whose image entry is a tiny MBR disk with a
// hand-made FAT32 boot partition holding kernel.img and a long-named
// bcm2708-rpi-zero.dtb — so the whole real code path runs: checksum, tar
// stream, base-256 / pax sizes and names, the FAT32 reader, the install swap.

namespace {

void put16(std::vector<uint8_t>& b, std::size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v); b[at + 1] = static_cast<uint8_t>(v >> 8);
}
void put32(std::vector<uint8_t>& b, std::size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

/// A 64-sector disk: MBR, one FAT32 (type 0x0C) partition at LBA 8 with one
/// sector per cluster, kernel.img (8.3) and bcm2708-rpi-zero.dtb (VFAT long
/// name) in its root directory. Small files only (one cluster each).
std::vector<uint8_t> fake_nextpi_disk(const std::string& kernel, const std::string& dtb) {
    std::vector<uint8_t> d(64 * 512, 0);
    const std::size_t part = 8 * 512;
    d[0x1BE + 4] = 0x0C;                 // FAT32 LBA
    put32(d, 0x1BE + 8, 8);              // first sector
    put32(d, 0x1BE + 12, 56);            // size
    d[510] = 0x55; d[511] = 0xAA;
    put16(d, part + 11, 512);            // bytes per sector
    d[part + 13] = 1;                    // sectors per cluster
    put16(d, part + 14, 1);              // reserved sectors
    d[part + 16] = 1;                    // FATs
    put32(d, part + 32, 56);             // total sectors
    put32(d, part + 36, 1);              // sectors per FAT
    put32(d, part + 44, 2);              // root cluster
    std::memcpy(&d[part + 82], "FAT32   ", 8);
    d[part + 510] = 0x55; d[part + 511] = 0xAA;
    const std::size_t fat = part + 512;  // LBA 9
    put32(d, fat + 0, 0x0FFFFFF8);
    put32(d, fat + 4, 0x0FFFFFFF);
    for (int cl = 2; cl <= 4; ++cl) put32(d, fat + 4 * cl, 0x0FFFFFFF);   // root, kernel, dtb
    const auto cluster = [&](int cl) { return static_cast<std::size_t>((10 + cl - 2) * 512); };

    std::size_t e = cluster(2);
    auto sfn = [&](const char* name11, int cl, std::size_t size) {
        std::memcpy(&d[e], name11, 11);
        d[e + 11] = 0x20;
        put16(d, e + 26, static_cast<uint16_t>(cl));
        put32(d, e + 28, static_cast<uint32_t>(size));
        e += 32;
    };
    sfn("KERNEL  IMG", 3, kernel.size());
    // "bcm2708-rpi-zero.dtb" = 20 chars: two LFN slots, highest first.
    const std::string lfn = "bcm2708-rpi-zero.dtb";
    for (int slot = 2; slot >= 1; --slot) {
        d[e] = static_cast<uint8_t>(slot | (slot == 2 ? 0x40 : 0));
        d[e + 11] = 0x0F;
        static const int offs[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
        for (int k = 0; k < 13; ++k) {
            const std::size_t idx = static_cast<std::size_t>((slot - 1) * 13 + k);
            const uint16_t ch = idx < lfn.size() ? static_cast<uint8_t>(lfn[idx])
                              : idx == lfn.size() ? 0x0000 : 0xFFFF;
            put16(d, e + offs[k], ch);
        }
        e += 32;
    }
    sfn("BCM270~1DTB", 4, dtb.size());
    std::memcpy(&d[cluster(3)], kernel.data(), kernel.size());
    std::memcpy(&d[cluster(4)], dtb.data(), dtb.size());
    return d;
}

/// A tar header: name, size, type. `base256` writes the size in GNU's
/// base-256 form (how a >8 GB image, like NextPi's, is recorded).
std::string tar_header(const std::string& name, uint64_t size, char type, bool base256 = false) {
    std::string h(512, '\0');
    std::memcpy(&h[0], name.data(), std::min<std::size_t>(name.size(), 100));
    std::snprintf(&h[100], 8, "%07o", 0644);
    if (base256) {
        h[124] = static_cast<char>(0x80);
        for (int i = 0; i < 8; ++i) h[135 - i] = static_cast<char>(size >> (8 * i));
    } else {
        std::snprintf(&h[124], 12, "%011llo", static_cast<unsigned long long>(size));
    }
    h[156] = type;
    std::memcpy(&h[257], "ustar  ", 8);                      // GNU magic, like NextPi's
    std::memset(&h[148], ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : h) sum += c;
    std::snprintf(&h[148], 8, "%06o", sum);
    return h;
}
std::string tar_entry(const std::string& name, const std::string& data, char type = '0',
                      bool base256 = false) {
    std::string out = tar_header(name, data.size(), type, base256) + data;
    out.resize((out.size() + 511) / 512 * 512, '\0');
    return out;
}
std::string pax_record(const std::string& key, const std::string& value) {
    const std::string body = " " + key + "=" + value + "\n";
    std::size_t len = body.size() + 1;
    while (std::to_string(len).size() + body.size() != len) ++len;
    return std::to_string(len) + body;
}

/// NextPi-<release>.tar.gz as the mirror serves it: a directory, an md5 file,
/// a pax-named and base-256-sized image, written with zlib.
bool write_fake_release(const std::string& path, const std::string& release,
                        const std::vector<uint8_t>& disk) {
    const std::string top = "NextPi-" + release + "/";
    std::string tar = tar_entry(top, "", '5');
    tar += tar_entry(top + "NextPi2-test.img.md5", "0123 x\n");
    tar += tar_entry(top + "PaxHeaders/img", pax_record("path", top + "NextPi2-test.img"), 'x');
    tar += tar_entry("ignored-short-name", std::string(disk.begin(), disk.end()), '0', true);
    tar += std::string(1024, '\0');
    gzFile gz = gzopen(path.c_str(), "wb");
    if (!gz) return false;
    const bool ok = gzwrite(gz, tar.data(), static_cast<unsigned>(tar.size())) == static_cast<int>(tar.size());
    return gzclose(gz) == Z_OK && ok;
}

/// A mirror directory plus a download seam that serves from it (and the
/// listing page for "<mirror>/"), counting what it was asked for.
struct FakeMirror {
    std::filesystem::path dir;
    std::string           listing;
    std::vector<std::string> fetched;
    bool                  offline = false;

    explicit FakeMirror(const std::string& tag) {
        dir = std::filesystem::temp_directory_path() / ("jnext-nextpi-mirror-" + tag + "-" + pid_tag());
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }
    ~FakeMirror() { std::error_code ec; std::filesystem::remove_all(dir, ec); }

    std::string url() const { return "https://mirror.test/NextPi2"; }

    bool add_release(const std::string& release, const std::vector<uint8_t>& disk) {
        const std::string archive = (dir / ("NextPi-" + release + ".tar.gz")).string();
        if (!write_fake_release(archive, release, disk)) return false;
        std::ofstream(archive + ".md5") << "MD5 (NextPi-" << release << ".tar.gz) = "
                                        << nextpi::md5_file(archive) << "\n";
        listing += "<a href=\"NextPi-" + release + ".tar.gz\">NextPi-" + release + ".tar.gz</a>\n"
                   "<a href=\"NextPi-" + release + ".tar.gz.md5\">md5</a>\n";
        return true;
    }

    sdcard::DownloadFn download() {
        return [this](const std::string& u, const std::string& dest, const sdcard::ProgressFn& progress,
                      std::string& err) {
            fetched.push_back(u);
            if (offline) { err = "Could not resolve hostname"; return false; }
            if (u == url() + "/") { std::ofstream(dest) << listing; return true; }
            const std::string name = u.substr(u.find_last_of('/') + 1);
            std::error_code ec;
            std::filesystem::copy_file(dir / name, dest,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) { err = "HTTP status 404"; return false; }
            if (progress) progress(1, 1);
            return true;
        };
    }
};

std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

nextpi::ProvisionOptions nextpi_options(FakeMirror& mirror, const std::string& dir,
                                        const std::string& release, int& confirms, bool answer = true) {
    nextpi::ProvisionOptions o;
    o.dir          = dir;
    o.release      = release;
    o.mirror       = mirror.url();
    o.download     = mirror.download();
    o.confirm      = [&confirms, answer](const std::string&) { ++confirms; return answer; };
    o.space_needed = 0;
    return o;
}

} // namespace


// ══════════════════════════════════════════════════════════════════════
// NextPi — behaviours the first review found untested (PR #310)
// ══════════════════════════════════════════════════════════════════════

namespace {

/// Wait up to `ms` for `pid` to be gone (reaped or never ours: kill fails).
bool gone_within(int pid, int ms) {
    for (int waited = 0; waited <= ms; waited += 20) {
        if (!process_alive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

/// A tar.gz of `entries` (already-built tar blocks) plus the end marker.
bool write_tar_gz(const std::string& path, const std::string& tar_body) {
    const std::string tar = tar_body + std::string(1024, '\0');
    gzFile gz = gzopen(path.c_str(), "wb");
    if (!gz) return false;
    const bool ok = gzwrite(gz, tar.data(), static_cast<unsigned>(tar.size())) == static_cast<int>(tar.size());
    return gzclose(gz) == Z_OK && ok;
}

} // namespace


// ══════════════════════════════════════════════════════════════════════
// NextPi — behaviours the second review found untested (PR #310, R2)
// ══════════════════════════════════════════════════════════════════════

namespace {

/// The descriptors this process has open below 1024.
std::set<int> open_fds() {
    std::set<int> fds;
#ifndef _WIN32
    for (int fd = 0; fd < 1024; ++fd)
        if (::fcntl(fd, F_GETFD) >= 0) fds.insert(fd);
#endif
    return fds;
}

} // namespace



// ── Main ──────────────────────────────────────────────────────────────
