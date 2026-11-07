// UART integration rows that need the emulated ESP-01 (ESP-01..04).
//
// A live emulated ESP runs its AT engine on a worker thread (ThreadedEsp), which the
// WebAssembly build does not have (and refuses --esp), so these rows moved, IDs,
// fixtures and assertions unchanged, out of uart_integration_test into a suite that is
// `# os: native` in test/unit-tests.conf. The scaffolding is shared with
// uart_integration_test through uart_integration_includes.h +
// uart_integration_helpers.h.
//
// Run: ./build/test/uart_integration_native_test

#include "uart_integration_includes.h"

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

#include "uart_integration_helpers.h"

static void test_esp_backend() {
    set_group("ESP");

    // ESP-01 — guest TX egresses to the real ESP, which answers. Two halves,
    // and the second is the one a wiring mistake trips: an unattached channel
    // loops the guest's own bytes into its own RX FIFO (uart.cpp
    // deliver_tx_byte), so "AT\r\n" coming back would look like traffic while
    // proving the ESP is NOT there. Echo is off by default (design doc
    // simplification 2), so the ESP never sends the guest's bytes back.
    {
        Emulator emu;
        emu.init(esp_config(true));
        uart0_send(emu, "AT\r\n");
        const Drained reply = uart0_drain(emu, "\r\nOK\r\n");

        check("ESP-01",
              "guest TX on UART 0 egresses to the REAL emulated ESP-01, which parses "
              "the AT line and answers — not to the channel's loopback "
              "[zxnext.vhd:1611-1612, :3381 UART 0 = ESP]",
              reply.bytes == "\r\nOK\r\n",
              fmt("RX drained %zu bytes %s: '%s' (want '\\r\\nOK\\r\\n'; 'AT\\r\\n' would "
                  "mean loopback, empty would mean nothing is attached)",
                  reply.bytes.size(), waited(reply).c_str(), visible(reply.bytes).c_str()));
    }

    // ESP-02 — the same reply reaches the guest through the real RX FIFO AND
    // drives the real IM2 UART0_RX vector under the NR 0xC6 request mask
    //   near_full OR (avail AND NOT nr_c6(1))
    // (zxnext.vhd:1941-1944). Asserting BOTH directions is what makes it
    // discriminative: a delivery path that bypassed `Uart::inject_rx` could
    // still hand the guest the bytes but could not reproduce the mask. Six
    // bytes never reach the 3/4 near-full mark, so bit 1 really does suppress.
    {
        Emulator emu;
        emu.init(esp_config(true));
        nr_write(emu, 0xC6, 0x01);              // per-byte avail contributes
        uart0_send(emu, "AT\r\n");
        const Drained got_avail = uart0_drain(emu, "\r\nOK\r\n");
        const uint8_t ca_avail  = nr_read(emu, 0xCA);

        Emulator emu2;
        emu2.init(esp_config(true));
        nr_write(emu2, 0xC6, 0x02);             // near-full only; avail suppressed
        uart0_send(emu2, "AT\r\n");
        const Drained got_masked = uart0_drain(emu2, "\r\nOK\r\n");
        const uint8_t ca_masked  = nr_read(emu2, 0xCA);

        check("ESP-02",
              "the ESP's reply lands in the UART 0 RX FIFO and raises the UART0_RX "
              "IM2 vector under the NR 0xC6 request mask near_full OR (avail AND NOT "
              "bit1) [zxnext.vhd:1941-1944, :1949-1950]",
              got_avail.bytes == "\r\nOK\r\n" && (ca_avail & 0x03) != 0 &&
                  got_masked.bytes == "\r\nOK\r\n" && (ca_masked & 0x03) == 0,
              fmt("C6=0x01: rx='%s' %s NR_CA=0x%02X (want reply + bits1:0 set); "
                  "C6=0x02: rx='%s' %s NR_CA=0x%02X (want reply + bits1:0 clear)",
                  visible(got_avail.bytes).c_str(), waited(got_avail).c_str(), ca_avail,
                  visible(got_masked.bytes).c_str(), waited(got_masked).c_str(), ca_masked));
    }

    // ESP-03 — RESTATED to what v1.0 actually built, deliberately.
    //
    // The row used to claim NR 0x02 bit 7 "resets the attached ESP-01, as
    // nextsync's recovery path does". The hardware line is real —
    // zxnext.vhd:5119 latches `nr_02_bus_reset <= nr_wr_dat(7)`, zxnext.vhd:1579
    // drives `o_RESET_PERIPHERAL` from it, and nextreg.txt:48 reads verbatim
    // "Assert and hold reset to the expansion bus and the esp wifi" — and jnext
    // latches the bit and reads it back correctly. What jnext does NOT do is
    // route it to the device. That is a v1.1 extension point (design doc §4.2
    // and §10), left out on the stated grounds that it is a RECOVERY path
    // ("degraded, not blocking") and that the hook belongs on the NR 0x02
    // WRITE, never on `UartChannel::reset`, which resets only the Next-side
    // state machine (§4.3).
    //
    // So the row pins the fact rather than the aspiration — and it is not a
    // tautology: `ATE1` leaves observable state inside the AT engine (echo on),
    // and a real hard reset would restore the power-on default of echo OFF
    // (simplification 2). Wire the reset up and this row goes red, which is
    // exactly right: the design record has to change with it.
    {
        Emulator emu;
        emu.init(esp_config(true));

        // WAIT for the ATE1 acknowledgement, do not merely allow time for it:
        // everything below asserts that the engine kept the state this command
        // set, so a run in which the command had not been PARSED yet would be
        // asserting nothing (GH #186).
        uart0_send(emu, "ATE1\r\n");             // echo ON: engine state to lose
        const Drained ack = uart0_drain(emu, "\r\nOK\r\n");

        nr_write(emu, 0x02, 0x80);               // assert the ESP / expbus reset
        const uint8_t nr02 = nr_read(emu, 0x02);
        uart0_drain(emu, "");                    // expect silence: settle only

        // With echo on, the reply to `AT` is the echoed command AND the OK.
        uart0_send(emu, "AT\r\n");
        const Drained after = uart0_drain(emu, "AT\r\n\r\nOK\r\n");

        check("ESP-03",
              "NR 0x02 bit 7 (o_RESET_PERIPHERAL) latches and reads back, and in v1.0 "
              "drives NO device reset — the attached ESP keeps its state across it; "
              "nextsync's recovery path is a v1.1 extension point (design doc §4.2) "
              "[zxnext.vhd:5119, :1579; nextreg.txt:48]",
              (nr02 & 0x80) != 0 && ack.bytes == "\r\nOK\r\n" &&
                  after.bytes.find("AT") != std::string::npos,
              fmt("NR 0x02 readback=0x%02X (want b7 set); ATE1 ack='%s' %s (want "
                  "'\\r\\nOK\\r\\n', else the echo was never turned on); reply after "
                  "the reset='%s' %s (want the ATE1 echo still on — a bare "
                  "'\\r\\nOK\\r\\n' would mean the engine had been reset)",
                  nr02, visible(ack.bytes).c_str(), waited(ack).c_str(),
                  visible(after.bytes).c_str(), waited(after).c_str()));
    }

    // ESP-04 — the converse, and the reason ESP-01's second half is worth
    // asserting: with nothing attached, UART 0 keeps the loopback the 116
    // pre-ESP UART rows observe. This is what "--esp is default off" means at
    // the wire, rather than at the config struct.
    {
        Emulator emu;
        emu.init(esp_config(false));
        uart0_send(emu, "AT\r\n");
        const Drained reply = uart0_drain(emu, "AT\r\n");

        check("ESP-04",
              "with no ESP backend attached, UART 0 keeps its loopback: the guest's "
              "own bytes come back and nothing answers them",
              emu.uart().device(0) == nullptr && reply.bytes == "AT\r\n",
              fmt("device=%p rx='%s' %s (want null + 'AT\\r\\n')",
                  static_cast<const void*>(emu.uart().device(0)),
                  visible(reply.bytes).c_str(), waited(reply).c_str()));
    }
}


int main() {
    std::printf("UART integration: ESP rows (need a thread)\n");
    std::printf("===============================================\n\n");

    test_esp_backend();
    std::printf("  Group: ESP — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());
    return g_fail > 0 ? 1 : 0;
}
