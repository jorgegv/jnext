// UART integration rows that need POSIX facilities (GH #214).
//
// The rows that used to close uart_integration_test and have no Windows
// counterpart live here, `# os: posix` in test/unit-tests.conf:
//
//   * JOY-15..22  the joystick-port cable over real FIFOs and a pty
//                 (--joy-uart-rx/-tx, --joy-uart-pty), which joy_uart_link.cpp
//                 refuses on Windows;
//   * PI-*        the live Raspberry Pi serial link (--nextpi): FIFOs, a
//                 stand-in qemu-system-arm started as a child process, file
//                 descriptor inheritance, kill(2), signals.
//
// Rows, IDs and assertions are unchanged; the scaffolding is shared with
// uart_integration_test through uart_integration_includes.h + uart_integration_helpers.h.
//
// Run: ./build/test/uart_posix_test

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

static void test_joy_uart_cable_posix() {
    set_group("JOY");

    // ── JOY-15 — THE TWO DIRECTIONS ARE GATED DIFFERENTLY, and that asymmetry
    // is the VHDL's:
    //
    //   * RX is connector-selected. `joy_uart_rx <= ((not nr_0b_joy_iomode(0))
    //     and not i_JOY_LEFT(5)) or (nr_0b_joy_iomode(0) and not
    //     i_JOY_RIGHT(5))` (zxnext.vhd:3538) — bit 4 picks ONE socket's pin to
    //     listen on.
    //   * TX is not. `o_JOY_IO_MODE_PIN_7` (zxnext.vhd:1593) has no connector
    //     selection anywhere in it; the board presents it to whichever socket
    //     `o_joy_select` currently points at, and in io mode that free-runs
    //     (`state <= "1111100" & state_next(1 downto 0)`, o_joy_select <=
    //     state(1) — md6_joystick_connector_x2.vhd:109,117), alternating every
    //     two CLK_28 ticks while one bit at 115200 lasts 243 of them. Both
    //     sockets therefore see the transmitted bit.
    //
    // So a cable in the socket bit 4 does NOT select can still HEAR the Next
    // while being unable to be heard by it. Copying the RX gate onto the TX
    // path — the obvious-looking symmetry — would silently half-break exactly
    // that rig, and nothing else in this suite would notice.
    {
        TempFifoCable cable("asym");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/0));      // cable in joy 1
        const bool attached = cable.attach();

        // NR 0x0B bit 4 = 1 selects joy 2 — the OTHER socket.
        cable.send({0x11, 0x22});
        guest_transmit_frame(emu, 0xB0, 0, 0x99);
        emu.nextreg().write(0x0B, 0xB0);
        emu.run_frame();

        const JoyUartLink* link = emu.joy_uart_link();
        const std::vector<uint8_t> heard_by_host = cable.drain();
        const bool rx_dropped = link && link->delivered() == 0 && link->dropped() > 0;
        const bool rx_fifo_empty = emu.uart().channel(0).rx_empty();
        const bool tx_arrived = (heard_by_host.size() == 1) && (heard_by_host[0] == 0x99);

        check("JOY-15",
              "zxnext.vhd:3538 vs :1593 + md6_joystick_connector_x2.vhd:109,117 "
              "— NR 0x0B bit 4 selects which socket is LISTENED to, while pin 7 "
              "is presented to both sockets in turn, so a cable in the "
              "unselected socket is not heard by the Next yet still hears it",
              attached && rx_dropped && rx_fifo_empty && tx_arrived,
              fmt("attached=%d rx_dropped=%d rx_fifo_empty=%d tx_arrived=%d "
                  "(all want 1); delivered=%zu dropped=%zu; host heard [%s] "
                  "(want 99)",
                  attached ? 1 : 0, rx_dropped ? 1 : 0, rx_fifo_empty ? 1 : 0,
                  tx_arrived ? 1 : 0, link ? link->delivered() : 0,
                  link ? link->dropped() : 0, bytes_hex(heard_by_host).c_str()));
    }

    // ── JOY-16 — THE FEATURE. A live FIFO cable carries both directions of a
    // conversation while the machine runs: the host's bytes cross the
    // zxnext.vhd:3340-3341 RX mux into the channel the guest reads at port
    // 0x143B, and the guest's bytes come back out of the zxnext.vhd:3526-3531
    // TX mux onto a descriptor the host reads.
    //
    // Real descriptors, real `mkfifo`, real `open`. That is the point: the
    // one-way path this replaces failed not in its arithmetic but at `open()`
    // — a FIFO blocked until its writer closed and a socket returned ENXIO —
    // and no amount of in-memory stubbing would have found either.
    {
        TempFifoCable cable("duplex");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));      // cable in joy 2
        const bool attached = cable.attach();

        const std::vector<uint8_t> from_host = {0x4A, 0x4E, 0x58, 0x54};
        cable.send(from_host);

        std::vector<uint8_t> to_guest;
        for (int f = 0; f < 3; ++f) {
            emu.nextreg().write(0x0B, 0xB0);      // mux on, channel 0, joy 2
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                to_guest.push_back(emu.port().in(0x143B));
        }

        const std::vector<uint8_t> from_guest = {0x01, 0x02, 0x03};
        for (uint8_t b : from_guest) guest_transmit_frame(emu, 0xB0, 0, b);
        const std::vector<uint8_t> to_host = cable.drain();

        const JoyUartLink* link = emu.joy_uart_link();

        check("JOY-16",
              "GH #252 — a live joystick-port cable carries both directions at "
              "once: the host's bytes reach port 0x143B through the "
              "zxnext.vhd:3340-3341 RX mux and the guest's reach the host "
              "through the zxnext.vhd:3526-3531 pin-7 TX mux, over real FIFOs "
              "opened while the machine runs",
              attached && to_guest == from_host && to_host == from_guest
                  && link && link->dropped() == 0 && link->unsent() == 0,
              fmt("attached=%d; guest got [%s] (want [4A 4E 58 54]); host got "
                  "[%s] (want [01 02 03]); delivered=%zu dropped=%zu sent=%zu "
                  "unsent=%zu",
                  attached ? 1 : 0, bytes_hex(to_guest).c_str(),
                  bytes_hex(to_host).c_str(),
                  link ? link->delivered() : 0, link ? link->dropped() : 0,
                  link ? link->sent() : 0, link ? link->unsent() : 0));
    }

    // ── JOY-17 — the live cable's RX is PACED at the receiving channel's byte
    // time, not handed over as fast as the host supplies it.
    //
    // uart_rx.vhd clocks one frame per `prescaler * frame_bits` CLK_28 ticks
    // (uart.vhd:404 wires the channel's own prescaler into its receiver), and
    // the Next-side RX FIFO is 512 entries with drop-newest overflow. A host
    // that writes 3000 bytes in one go — one DeZog memory-read response — would
    // therefore lose 5/6 of them to the FIFO before the guest's first read if
    // the cable simply dumped what it had. The count below is the elapsed
    // master cycles over the channel's byte time, ±1 for where the run's edges
    // fall, which is three thousand away from "everything at once".
    {
        TempFifoCable cable("pace");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        std::vector<uint8_t> burst(3000);
        for (std::size_t i = 0; i < burst.size(); ++i)
            burst[i] = static_cast<uint8_t>((i * 11 + 3) & 0xFF);
        const std::size_t offered = cable.send(burst);

        const uint64_t start = emu.clock().get();
        std::vector<uint8_t> got;
        for (int f = 0; f < 2; ++f) {
            emu.nextreg().write(0x0B, 0xB0);
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                got.push_back(emu.port().in(0x143B));
        }
        const uint64_t cycles     = emu.clock().get() - start;
        const uint32_t byte_ticks = emu.uart().channel(0).byte_transfer_ticks();
        const uint64_t want       = cycles / byte_ticks;

        // The prefix must be the stream's, in order — a paced-but-scrambled
        // delivery would satisfy a bare count.
        bool prefix_ok = got.size() <= burst.size();
        for (std::size_t i = 0; prefix_ok && i < got.size(); ++i)
            prefix_ok = (got[i] == burst[i]);

        check("JOY-17",
              "uart.vhd:404 / uart_rx.vhd — the live cable delivers at the "
              "receiving channel's byte time (prescaler * frame_bits), so a "
              "3000-byte host burst arrives over many frames in order rather "
              "than overflowing the 512-entry RX FIFO in one",
              attached && offered >= burst.size() && prefix_ok
                  && got.size() + 1 >= want && got.size() <= want + 1
                  && got.size() < burst.size(),
              fmt("attached=%d offered=%zu (want 3000); got %zu byte(s) (want "
                  "%llu±1 and < 3000); in-order prefix=%d; byte_ticks=%u over "
                  "%llu cycles",
                  attached ? 1 : 0, offered, got.size(),
                  static_cast<unsigned long long>(want), prefix_ok ? 1 : 0,
                  byte_ticks, static_cast<unsigned long long>(cycles)));
    }

    // ── JOY-18 — NOTHING BLOCKS WHEN THERE IS NO FAR END, and a far end that
    // turns up late is picked up.
    //
    // This is the defect GH #252 reports, in its most literal form: the
    // Next→host FIFO cannot be opened at all until something is reading it
    // (`O_WRONLY | O_NONBLOCK` gives ENXIO), so an eager open would either fail
    // the run or — with the blocking form — wedge the emulator before the first
    // frame. The cable opens that side LAZILY and retries it on every flush, so
    // a debugger started after the emulator still gets the traffic that was
    // queued while it was not there.
    {
        TempFifoCable cable("late");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach(/*open_tx_reader=*/false);

        const std::vector<uint8_t> markers = {0x71, 0x72, 0x73};
        for (uint8_t b : markers) guest_transmit_frame(emu, 0xB0, 0, b);

        const JoyUartLink* link = emu.joy_uart_link();
        const bool nothing_sent_yet = link && link->sent() == 0
                                   && link->unsent() == 0 && link->faults() == 0;

        const bool reader_opened = cable.open_tx_reader_now();
        emu.nextreg().write(0x0B, 0xB0);
        emu.run_frame();                        // the frame seam retries the open
        const std::vector<uint8_t> got = cable.drain();

        check("JOY-18",
              "GH #252 — the Next->host FIFO is opened lazily because "
              "O_WRONLY|O_NONBLOCK on a FIFO with no reader is ENXIO: with no "
              "peer the run proceeds and nothing is lost or faulted, and a "
              "reader that attaches later receives what was queued",
              attached && nothing_sent_yet && reader_opened && got == markers,
              fmt("attached=%d nothing_sent_yet=%d reader_opened=%d (all want "
                  "1); got [%s] (want [71 72 73]); sent=%zu unsent=%zu faults=%zu",
                  attached ? 1 : 0, nothing_sent_yet ? 1 : 0,
                  reader_opened ? 1 : 0, bytes_hex(got).c_str(),
                  link ? link->sent() : 0, link ? link->unsent() : 0,
                  link ? link->faults() : 0));
    }

    // ── JOY-19 — THE FAR END DISAPPEARS MID-SESSION, and comes back.
    //
    // Closing the read end of a FIFO makes the next write fail with EPIPE —
    // and, with the default signal disposition, raise SIGPIPE and KILL the
    // process. jnext ignores SIGPIPE when it opens the endpoint, takes the
    // EPIPE, and then DISCARDS what is queued: those bytes are half of a
    // conversation with a process that has exited, and giving the remains of
    // them to whatever connects next would hand that peer a truncated message
    // it cannot recognise as stale. The loss is counted rather than silent, and
    // the cable re-opens for the next peer.
    {
        TempFifoCable cable("bounce");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        guest_transmit_frame(emu, 0xB0, 0, 0xA1);
        const std::vector<uint8_t> before = cable.drain();

        cable.close_host_tx_reader();
        guest_transmit_frame(emu, 0xB0, 0, 0xA2);      // EPIPE: lost with the peer

        const JoyUartLink* link = emu.joy_uart_link();
        const bool survived   = (link != nullptr);
        const bool loss_seen  = link && link->unsent() == 1 && link->sent() == 1;

        const bool reopened = cable.open_tx_reader_now();
        guest_transmit_frame(emu, 0xB0, 0, 0xA3);
        const std::vector<uint8_t> after = cable.drain();

        check("JOY-19",
              "GH #252 — a peer that closes mid-session makes the next write "
              "EPIPE (SIGPIPE is ignored, so the emulator survives it); the "
              "stale queue is discarded and counted rather than delivered to "
              "the next peer, and the cable re-opens for one that reconnects",
              attached && survived && before == std::vector<uint8_t>{0xA1}
                  && loss_seen && reopened && after == std::vector<uint8_t>{0xA3},
              fmt("attached=%d survived=%d loss_seen=%d reopened=%d (all want "
                  "1); before=[%s] (want A1) after=[%s] (want A3); sent=%zu "
                  "unsent=%zu faults=%zu",
                  attached ? 1 : 0, survived ? 1 : 0, loss_seen ? 1 : 0,
                  reopened ? 1 : 0, bytes_hex(before).c_str(),
                  bytes_hex(after).c_str(), link ? link->sent() : 0,
                  link ? link->unsent() : 0, link ? link->faults() : 0));
    }

    // ── JOY-20 — THE REPLAY GATE. A live descriptor is not snapshottable and
    // nothing about this cable rides in the state stream, which is the opposite
    // of the GH #251 recording (JOY-10/11) and for a reason the recording does
    // not have: bytes already handed to the peer cannot be unsent.
    //
    // So while `replay_mode_` holds — rewind fast-forward and RZX playback both
    // re-execute instructions the guest already ran — the cable is INERT, the
    // `EspUartAdapter::set_inert` posture. Two halves, and the second is the one
    // a naive gate gets wrong: a replayed frame must not re-transmit (the peer
    // would see the byte twice), and it must not READ either, because the host
    // bytes it consumed would be gone from the timeline that resumes afterwards.
    {
        TempFifoCable cable("replay");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        guest_transmit_frame(emu, 0xB0, 0, 0xD1);
        const std::vector<uint8_t> live_before = cable.drain();

        emu.set_replay_mode(true);
        cable.send({0xE1, 0xE2});                   // host talks during the replay
        guest_transmit_frame(emu, 0xB0, 0, 0xD2);   // guest re-transmits
        const std::vector<uint8_t> during = cable.drain();
        const JoyUartLink* link = emu.joy_uart_link();
        const bool not_read = link && link->received() == 0;

        emu.set_replay_mode(false);
        std::vector<uint8_t> to_guest;
        for (int f = 0; f < 3; ++f) {
            emu.nextreg().write(0x0B, 0xB0);
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                to_guest.push_back(emu.port().in(0x143B));
        }
        guest_transmit_frame(emu, 0xB0, 0, 0xD3);
        const std::vector<uint8_t> live_after = cable.drain();

        check("JOY-20",
              "GH #252 — the live cable is held inert while replay_mode_ holds "
              "(the EspUartAdapter::set_inert posture): a re-executed frame "
              "neither re-transmits to the peer nor consumes the host bytes the "
              "resumed timeline still needs, and both directions return when "
              "the gate lifts",
              attached && live_before == std::vector<uint8_t>{0xD1}
                  && during.empty() && not_read
                  && to_guest == std::vector<uint8_t>({0xE1, 0xE2})
                  && live_after == std::vector<uint8_t>{0xD3},
              fmt("attached=%d not_read=%d (both want 1); before=[%s] (want D1) "
                  "during=[%s] (want empty) after=[%s] (want D3); guest got [%s] "
                  "(want [E1 E2]); received=%zu",
                  attached ? 1 : 0, not_read ? 1 : 0,
                  bytes_hex(live_before).c_str(), bytes_hex(during).c_str(),
                  bytes_hex(live_after).c_str(), bytes_hex(to_guest).c_str(),
                  link ? link->received() : 0));
    }

    // ── JOY-21 — the PTY form of the same cable, end to end.
    //
    // A pty is what the consumer this issue names can actually open: DeZog's
    // serial remote wants one serial DEVICE, not a pair of pipes, so without
    // this transport the feature reaches a shell script and stops. It is also
    // the form with a different failure surface — one descriptor for both
    // directions, a line discipline that echoes and mangles CR/LF unless the
    // termios is put in raw mode, and a master that survives its slave closing
    // — so exercising it through the FIFO's rows would prove nothing about it.
    //
    // The slave path is read back from the cable's own description, which is
    // also the only way a user learns it.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        cfg.joy_uart_pty         = true;
        cfg.joy_uart_connector   = 1;
        emu.init(cfg);

        const JoyUartLink* link = emu.joy_uart_link();
        std::string slave;
        if (link) {
            const std::string& d = link->describe();
            const std::size_t at = d.find("pty ");
            if (at != std::string::npos) slave = d.substr(at + 4);
        }

        int slave_fd = -1;
#ifndef _WIN32
        if (!slave.empty())
            slave_fd = ::open(slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
#endif
        const bool opened = slave_fd >= 0;

        std::vector<uint8_t> to_guest;
        std::vector<uint8_t> to_host;
        if (opened) {
#ifndef _WIN32
            const uint8_t out[] = {0x31, 0x32};
            (void)!::write(slave_fd, out, sizeof(out));
#endif
            for (int f = 0; f < 3; ++f) {
                emu.nextreg().write(0x0B, 0xB0);
                emu.run_frame();
                emu.port().out(0x153B, 0x00);
                while (!emu.uart().channel(0).rx_empty())
                    to_guest.push_back(emu.port().in(0x143B));
            }
            guest_transmit_frame(emu, 0xB0, 0, 0x7E);
            guest_transmit_frame(emu, 0xB0, 0, 0x0D);   // CR: raw mode must not translate it
#ifndef _WIN32
            uint8_t buf[64];
            for (int tries = 0; tries < 4 && to_host.size() < 2; ++tries) {
                const ssize_t n = ::read(slave_fd, buf, sizeof(buf));
                if (n > 0) to_host.insert(to_host.end(), buf, buf + n);
                emu.nextreg().write(0x0B, 0xB0);
                emu.run_frame();
            }
            ::close(slave_fd);
#endif
        }

        check("JOY-21",
              "GH #252 — the pty transport carries the same zxnext.vhd:3340-3341 "
              "/ :3526-3531 mux in both directions over one descriptor, with the "
              "termios in raw mode so a 0x0D is delivered as a byte rather than "
              "translated by the line discipline",
              opened && to_guest == std::vector<uint8_t>({0x31, 0x32})
                  && to_host == std::vector<uint8_t>({0x7E, 0x0D}),
              fmt("slave='%s' opened=%d; guest got [%s] (want [31 32]); host got "
                  "[%s] (want [7E 0D])",
                  slave.c_str(), opened ? 1 : 0, bytes_hex(to_guest).c_str(),
                  bytes_hex(to_host).c_str()));
    }

    // ── JOY-22 — the peer-loss discard throws away the RECEIVE queue too, and
    // that half is invisible from outside unless a row puts bytes in it.
    //
    // `flush_tx()` clears BOTH queues when a write reports EPIPE, because both
    // hold half of an exchange with a process that has exited. JOY-19 covers
    // the transmit half — it is the one the counters show. The receive half is
    // host bytes already pulled off the descriptor and not yet clocked into the
    // guest, and nothing about them is visible at the moment they are dropped:
    // deleting `rx_queue_.clear()` left the whole suite green, so that half of
    // the documented decision lived only in a comment.
    //
    // The shape that makes it observable is a burst BIGGER than one frame's
    // worth of pacing. 2000 bytes at the 115200 default is ~230 per frame, so
    // after one frame ~1770 are still queued when the peer goes away — and if
    // they were kept, the frames after it would go on delivering them.
    {
        TempFifoCable cable("rxdiscard");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        std::vector<uint8_t> burst(2000);
        for (std::size_t i = 0; i < burst.size(); ++i)
            burst[i] = static_cast<uint8_t>((i * 5 + 9) & 0xFF);
        const std::size_t offered = cable.send(burst);

        // One frame: the whole burst comes off the descriptor into the queue,
        // and only a frame's worth of it reaches the guest. The guest also
        // TRANSMITS here, and that is not decoration — the Next->host FIFO is
        // opened lazily, so until a byte has actually gone out jnext holds no
        // write descriptor and a peer closing is indistinguishable from one
        // that never attached (JOY-18's case, where nothing is discarded
        // because nothing was lost). Writing the row without this step made it
        // fail against correct code, which is how the distinction was found.
        std::vector<uint8_t> before;
        guest_transmit_frame(emu, 0xB0, 0, 0xA1);
        emu.port().out(0x153B, 0x00);
        while (!emu.uart().channel(0).rx_empty())
            before.push_back(emu.port().in(0x143B));
        const bool peer_was_attached = cable.drain() == std::vector<uint8_t>{0xA1};

        // NOW the peer goes away, and the guest's next transmitted byte is what
        // discovers it (EPIPE). Everything still queued dies with it.
        cable.close_host_tx_reader();
        guest_transmit_frame(emu, 0xB0, 0, 0xA2);

        const JoyUartLink* link = emu.joy_uart_link();
        const std::size_t delivered_at_loss = link ? link->delivered() : 0;
        // Bytes that came off the descriptor and never reached the sink: the
        // remainder that was in the queue at the instant of the loss. If this
        // is 0 the row is asserting nothing.
        const std::size_t abandoned = link
            ? link->received() - link->delivered() - link->dropped() : 0;

        // ...and now nothing more may arrive, however long the machine runs.
        std::vector<uint8_t> after;
        for (int f = 0; f < 3; ++f) {
            emu.nextreg().write(0x0B, 0xB0);
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                after.push_back(emu.port().in(0x143B));
        }

        // `dropped() == 0` throughout, so "nothing arrived" cannot be the mux
        // having stopped routing rather than the queue having been discarded.
        const bool still_routed = link && link->dropped() == 0;

        check("JOY-22",
              "GH #252 — a peer lost mid-session takes the RECEIVE queue with "
              "it as well as the transmit one: host bytes already read off the "
              "descriptor but not yet clocked into the guest are discarded, so "
              "the next peer's session does not begin with the tail of the "
              "previous one's message",
              attached && peer_was_attached && offered >= burst.size()
                  && !before.empty() && abandoned > 0 && after.empty()
                  && link->delivered() == delivered_at_loss && still_routed,
              fmt("attached=%d peer_was_attached=%d offered=%zu (want 2000); "
                  "before the loss the guest read %zu byte(s) (want >0) and %zu "
                  "were left queued (want >0); after it read %zu (want 0); "
                  "delivered %zu -> %zu (want unchanged); dropped=%zu (want 0)",
                  attached ? 1 : 0, peer_was_attached ? 1 : 0, offered,
                  before.size(), abandoned, after.size(), delivered_at_loss,
                  link ? link->delivered() : 0, link ? link->dropped() : 0));
    }
}

static void test_pi_uart_link() {
    set_group("PI");

    // ── PI-01 — THE FEATURE. With NR 0xA0 = 0x30 (UART 1 on GPIO 14/15, wired
    // for a Pi — what `.pisend` writes) a live link carries both directions:
    // the host's bytes are read by the guest at port 0x143B on UART 1, and the
    // guest's UART 1 transmissions reach the host. UART 0 hears none of it.
    {
        TempFifoCable cable("pi-duplex");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        const std::vector<uint8_t> from_host = {0x53, 0x55, 0x50, 0x3E};   // "SUP>"
        cable.send(from_host);
        std::vector<uint8_t> to_guest;
        for (int f = 0; f < 3; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            to_guest.insert(to_guest.end(), got.begin(), got.end());
        }

        const std::vector<uint8_t> from_guest = {0x0D, 0x03, 0x03};       // CR ^C ^C
        for (uint8_t b : from_guest) pi_transmit_frame(emu, 0x30, b);
        const std::vector<uint8_t> to_host = cable.drain();

        const PiUartDevice* pi = emu.pi_uart();
        const bool uart0_quiet = emu.uart().channel(0).rx_empty();

        check("PI-01",
              "the Raspberry Pi link carries both directions over UART 1 while NR "
              "0xA0 = 0x30 connects it to the Pi GPIO pins (zxnext.vhd:2278-2281): "
              "host bytes reach port 0x143B on UART 1, the guest's UART 1 bytes "
              "reach the host, and UART 0 hears nothing",
              pi && attached && to_guest == from_host && to_host == from_guest
                  && uart0_quiet && pi->link().dropped() == 0
                  && pi->tx_disconnected() == 0,
              fmt("pi=%d attached=%d; guest got [%s] (want [53 55 50 3E]); host got "
                  "[%s] (want [0D 03 03]); uart0_quiet=%d dropped=%zu "
                  "tx_disconnected=%zu",
                  pi ? 1 : 0, attached ? 1 : 0, bytes_hex(to_guest).c_str(),
                  bytes_hex(to_host).c_str(), uart0_quiet ? 1 : 0,
                  pi ? pi->link().dropped() : 0, pi ? pi->tx_disconnected() : 0));
    }

    // ── PI-02 — THE GPIO GATE. Bits 5 and 4 of NR 0xA0 put UART 1 on GPIO 14/15
    // and choose which way round RX and TX are wired; only both set reaches a Pi.
    // With either clear (0x00 = reset, 0x10, 0x20) the Pi's bytes are lost on a
    // pin the Next is not reading and the guest's never reach the Pi — counted,
    // not queued. Opening the gate afterwards proves the link itself was alive.
    {
        TempFifoCable cable("pi-gate");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        bool all_closed_silent = true;
        std::string detail;
        int closed_tx = 0;
        for (uint8_t a0 : {uint8_t{0x00}, uint8_t{0x10}, uint8_t{0x20}}) {
            cable.send({0xE0});
            std::vector<uint8_t> heard;
            for (int f = 0; f < 2; ++f) {
                const std::vector<uint8_t> got = pi_frame(emu, a0);
                heard.insert(heard.end(), got.begin(), got.end());
            }
            pi_transmit_frame(emu, a0, 0xF0);
            ++closed_tx;
            const std::vector<uint8_t> host = cable.drain();
            if (!heard.empty() || !host.empty()) all_closed_silent = false;
            detail += fmt("a0=%02X guest[%s] host[%s]; ", a0, bytes_hex(heard).c_str(),
                          bytes_hex(host).c_str());
        }

        const PiUartDevice* pi = emu.pi_uart();
        const std::size_t dropped_closed = pi ? pi->link().dropped() : 0;
        const std::size_t tx_lost_closed = pi ? pi->tx_disconnected() : 0;

        cable.send({0x31});
        std::vector<uint8_t> open_heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            open_heard.insert(open_heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0x32);
        const std::vector<uint8_t> open_host = cable.drain();

        check("PI-02",
              "NR 0xA0 bits 5:4 gate the Pi link both ways (zxnext.vhd:2278-2281): "
              "with 0x00, 0x10 or 0x20 the Pi is not heard and does not hear, and "
              "the loss is counted; 0x30 then carries traffic again",
              pi && attached && all_closed_silent && dropped_closed == 3
                  && tx_lost_closed == static_cast<std::size_t>(closed_tx)
                  && open_heard == std::vector<uint8_t>({0x31})
                  && open_host == std::vector<uint8_t>({0x32}),
              fmt("pi=%d attached=%d; %sdropped=%zu (want 3) tx_disconnected=%zu "
                  "(want 3); open: guest [%s] (want 31) host [%s] (want 32)",
                  pi ? 1 : 0, attached ? 1 : 0, detail.c_str(), dropped_closed,
                  tx_lost_closed, bytes_hex(open_heard).c_str(),
                  bytes_hex(open_host).c_str()));
    }

    // ── PI-03 — NO LOOPBACK. An unattached UART 1 loops its TX back into its own
    // RX FIFO (uart.h); with the Pi attached the byte goes to the Pi and the
    // guest must not read its own transmission back.
    {
        TempFifoCable cable("pi-noloop");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        pi_transmit_frame(emu, 0x30, 0x55);
        const std::vector<uint8_t> echoed = pi_frame(emu, 0x30);
        const std::vector<uint8_t> to_host = cable.drain();

        check("PI-03",
              "with the Pi link attached, a UART 1 transmission goes to the Pi "
              "only — UART 1's unattached loopback is off, so the guest does not "
              "read its own byte back",
              emu.pi_uart() && attached && echoed.empty()
                  && to_host == std::vector<uint8_t>({0x55}),
              fmt("pi=%d attached=%d; guest read back [%s] (want empty); host got "
                  "[%s] (want 55)",
                  emu.pi_uart() ? 1 : 0, attached ? 1 : 0,
                  bytes_hex(echoed).c_str(), bytes_hex(to_host).c_str()));
    }

    // ── PI-04 — THE JOYSTICK MUX STILL WINS. NR 0x0B = 0xB1 (UART mode, bits 7
    // and 5, channel bit 0 = UART 1) gives UART 1's RX to the joystick
    // connector (zxnext.vhd:3340-3341: `uart1_rx <= joy_uart_rx`, not
    // `pi_uart_rx`) and its TX to pin 7 (:3526-3531), so the Pi is neither heard
    // nor spoken to even with NR 0xA0 = 0x30. With NR 0x0B cleared it is again.
    {
        TempFifoCable cable("pi-joymux");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        cable.send({0xA1});
        std::vector<uint8_t> muxed_heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30, 0xB1);
            muxed_heard.insert(muxed_heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xA2, 0xB1);
        const std::vector<uint8_t> muxed_host = cable.drain();

        cable.send({0xA3});
        std::vector<uint8_t> free_heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30, 0x00);
            free_heard.insert(free_heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xA4, 0x00);
        const std::vector<uint8_t> free_host = cable.drain();

        check("PI-04",
              "while NR 0x0B routes UART 1 to the joystick connector "
              "(zxnext.vhd:3340-3341, :3526-3531) the Pi link is isolated in both "
              "directions; with the mux off it carries traffic again",
              emu.pi_uart() && attached && muxed_heard.empty() && muxed_host.empty()
                  && free_heard == std::vector<uint8_t>({0xA3})
                  && free_host == std::vector<uint8_t>({0xA4}),
              fmt("pi=%d attached=%d; muxed: guest [%s] host [%s] (want both "
                  "empty); free: guest [%s] (want A3) host [%s] (want A4)",
                  emu.pi_uart() ? 1 : 0, attached ? 1 : 0,
                  bytes_hex(muxed_heard).c_str(), bytes_hex(muxed_host).c_str(),
                  bytes_hex(free_heard).c_str(), bytes_hex(free_host).c_str()));
    }

    // ── PI-05 — A SOFT RESET DOES NOT UNPLUG THE PI. It resets the Next-side
    // UART and NR 0xA0 (to 0x00, zxnext.vhd:5080), not the Pi on the far end:
    // the same link object stays attached and carries traffic once the guest
    // routes UART 1 to the Pi again.
    {
        TempFifoCable cable("pi-softreset");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();
        const PiUartDevice* before = emu.pi_uart();

        emu.soft_reset();
        const PiUartDevice* after = emu.pi_uart();
        const bool same_device = before && before == after
                                 && emu.uart().device(1) == static_cast<const UartDevice*>(after);
        const uint8_t a0_after_reset = static_cast<uint8_t>(emu.nextreg().read(0xA0));

        cable.send({0x5A});
        std::vector<uint8_t> heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            heard.insert(heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0x5B);
        const std::vector<uint8_t> to_host = cable.drain();

        check("PI-05",
              "a soft reset keeps the Pi link attached to UART 1 (the Pi does not "
              "see a Next-side reset) while NR 0xA0 returns to 0x00 "
              "(zxnext.vhd:5080); traffic flows again once it is set",
              attached && same_device && a0_after_reset == 0x00
                  && heard == std::vector<uint8_t>({0x5A})
                  && to_host == std::vector<uint8_t>({0x5B}),
              fmt("attached=%d same_device=%d nr_a0=%02X (want 00); guest [%s] "
                  "(want 5A) host [%s] (want 5B)",
                  attached ? 1 : 0, same_device ? 1 : 0, a0_after_reset,
                  bytes_hex(heard).c_str(), bytes_hex(to_host).c_str()));
    }
}

static void test_pi_qemu() {
    set_group("PI");

    // ── PI-06 — the QEMU command line: a Raspberry Pi (QEMU raspi0) booting the
    // directory's kernel and device tree from the overlay, with its console
    // UART on the pipe chardev whose FIFOs jnext opens, no monitor and no
    // display. Paths with commas are escaped the QEMU way (doubled).
    {
        PiQemu::Spec spec;
        spec.dir = "/n";
        const std::vector<std::string> a = PiQemu::build_args(spec, "/run/uart");
        const bool shape =
            contains(a, "-M", "raspi0")
            && contains(a, "-kernel", "/n/boot/kernel.img")
            && contains(a, "-dtb", "/n/boot/bcm2708-rpi-zero.dtb")
            && contains(a, "-drive", "file=/n/overlay.qcow2,if=sd,format=qcow2")
            && contains(a, "-chardev", "pipe,id=pi,path=/run/uart")
            && contains(a, "-serial", "chardev:pi")
            && contains(a, "-monitor", "none")
            && contains(a, "-display", "none")
            && contains(a, "-device", "usb-audio,audiodev=snd0,buffer=16384");
        const std::string wav  = PiQemu::audiodev_arg("wav:/x,y.wav");
        const std::string none = PiQemu::audiodev_arg("none");
        const std::string dflt = PiQemu::audiodev_arg("");
#ifdef __APPLE__
        const bool dflt_ok = dflt.rfind("coreaudio,id=snd0", 0) == 0;
#else
        const bool dflt_ok = dflt == "pa,id=snd0";
#endif
        check("PI-06",
              "jnext builds a raspi0 command line booting the NextPi "
              "directory's kernel, device tree and overlay, with the console UART "
              "on the pipe chardev jnext opens; -audiodev is the platform default, "
              "a named driver, or wav:FILE",
              shape && wav == "wav,id=snd0,path=/x,,y.wav" && none == "none,id=snd0" && dflt_ok,
              fmt("shape=%d wav='%s' (want wav,id=snd0,path=/x,,y.wav) none='%s' "
                  "default='%s'",
                  shape ? 1 : 0, wav.c_str(), none.c_str(), dflt.c_str()));
    }

    // ── PI-07 — the refusals, each one a usage error before the machine boots
    // and none leaving a process behind: an incomplete NextPi directory, a
    // QEMU that is not installed, and a QEMU that dies at once.
    {
        FakeNextPi fake("refuse");
        std::string err_dir, err_bin, err_dies;

        PiQemu::Spec empty_dir;
        empty_dir.dir = (std::filesystem::temp_directory_path() / ("jnext-no-nextpi-" + pid_tag())).string();
        PiQemu a;
        const bool dir_refused = !a.start(empty_dir, err_dir)
                                 && err_dir.find("incomplete") != std::string::npos;

        PiQemu::Spec no_qemu;
        no_qemu.dir         = fake.dir();
        no_qemu.qemu_binary = fake.bin("no-such-qemu");
        PiQemu b;
        const bool bin_refused = !b.start(no_qemu, err_bin)
                                 && err_bin.find("not found") != std::string::npos && !b.running();

        PiQemu::Spec dies;
        dies.dir         = fake.dir();
        dies.qemu_binary = fake.bin("qemu-fails");
        PiQemu c;
        const bool dies_refused = !c.start(dies, err_dies)
                                  && err_dies.find("exited at once") != std::string::npos
                                  && !c.running();

        check("PI-07",
              "starting NextPi is refused before boot, leaving nothing running, when the "
              "NextPi directory is incomplete, when QEMU is not installed, "
              "and when QEMU exits at once",
              fake.ok() && dir_refused && bin_refused && dies_refused,
              fmt("fixture=%d dir='%s' binary='%s' dies='%s'", fake.ok() ? 1 : 0,
                  err_dir.c_str(), err_bin.c_str(), err_dies.c_str()));
    }

    // ── PI-08 — THE FEATURE, end to end with the stand-in. jnext creates the
    // overlay (via qemu-img) and the FIFOs, spawns "QEMU", and the Emulator
    // built from the resulting config hears the Pi's "SUP> " on UART 1 and is
    // heard by it, NR 0xA0 = 0x30 as `.pisend` sets it. Destroying the launcher
    // stops the process and removes the FIFOs.
    {
        FakeNextPi fake("e2e");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";

        auto qemu = std::make_unique<PiQemu>();
        std::string error;
        const bool started = fake.ok() && qemu->start(spec, error);
        const bool overlay = std::filesystem::exists(std::filesystem::path(fake.dir()) / "overlay.qcow2");
        const int  pid     = qemu->pid();
        const std::filesystem::path fifo_dir = std::filesystem::path(qemu->rx_path()).parent_path();

        std::vector<uint8_t> heard;
        bool sent = false;
        if (started) {
            Emulator emu;
            emu.init(pi_qemu_config(*qemu));
            for (int f = 0; f < 4 && heard.size() < 5; ++f) {
                const std::vector<uint8_t> got = pi_frame(emu, 0x30);
                heard.insert(heard.end(), got.begin(), got.end());
            }
            pi_transmit_frame(emu, 0x30, 0x0D);
            sent = fake.wait_received("\r");
        }
        const bool args_ok = fake.args().find("pipe,id=pi,path=") != std::string::npos;

        // SIGTERM ends QEMU at once; the 3 s SIGKILL fallback is for a QEMU
        // that ignores it, and must not be what stops a well-behaved one.
        const auto t0 = std::chrono::steady_clock::now();
        qemu.reset();
        const long stop_ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0).count());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const bool stopped     = !process_alive(pid) && stop_ms < 2000;
        const bool fifos_gone  = !fifo_dir.empty() && !std::filesystem::exists(fifo_dir);

        check("PI-08",
              "NextPi end to end: jnext creates the overlay and the FIFOs, "
              "starts QEMU on its pipe chardev, the guest reads the Pi's SUP> on "
              "UART 1 and the Pi receives the guest's byte; stopping the launcher "
              "ends the process with SIGTERM (not the SIGKILL fallback) and removes "
              "the FIFOs",
              started && overlay && args_ok
                  && heard == std::vector<uint8_t>({'S', 'U', 'P', '>', ' '}) && sent
                  && stopped && fifos_gone,
              fmt("started=%d (%s) overlay=%d args=%d guest heard [%s] (want 53 55 50 "
                  "3E 20) pi received=%d stopped=%d (%ld ms, want < 2000) fifos_gone=%d",
                  started ? 1 : 0, error.c_str(), overlay ? 1 : 0, args_ok ? 1 : 0,
                  bytes_hex(heard).c_str(), sent ? 1 : 0, stopped ? 1 : 0, stop_ms,
                  fifos_gone ? 1 : 0));
    }

    // ── PI-09 — A HARD RESET DOES NOT REBOOT THE PI. The launcher belongs to
    // the process, so a second Emulator built from the same config (what a
    // cold boot does) re-opens the same FIFOs and reaches the SAME Pi process,
    // which never saw the first Emulator go away.
    {
        FakeNextPi fake("coldboot");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error);
        const int  pid     = qemu.pid();

        bool first = false, second = false;
        if (started) {
            {
                Emulator emu;
                emu.init(pi_qemu_config(qemu));
                pi_transmit_frame(emu, 0x30, 'A');
                first = fake.wait_received("A");
            }
            Emulator emu;
            emu.init(pi_qemu_config(qemu));
            pi_transmit_frame(emu, 0x30, 'B');
            second = fake.wait_received("AB");
        }

        check("PI-09",
              "a rebuilt Emulator (a hard reset) reconnects to the same running Pi "
              "through the same FIFOs; the Pi received both machines' bytes in order",
              started && first && second && qemu.pid() == pid && process_alive(pid),
              fmt("started=%d (%s) first=%d second=%d received='%s' (want AB) "
                  "same_pid=%d alive=%d",
                  started ? 1 : 0, error.c_str(), first ? 1 : 0, second ? 1 : 0,
                  fake.received().c_str(), qemu.pid() == pid ? 1 : 0,
                  process_alive(pid) ? 1 : 0));
    }
}

static void test_nextpi_provisioner() {
    set_group("PI");
    namespace fs = std::filesystem;
    const std::vector<uint8_t> disk_a = fake_nextpi_disk("kernel image a", "device tree a");
    const std::vector<uint8_t> disk_b = fake_nextpi_disk("kernel image b", "device tree b");

    // ── PI-10 — releases: the mirror listing yields each NextPi-<name>.tar.gz
    // once (not its .md5), and names compare as `sort -V` does, so "latest"
    // picks 1_100 over 1_93D and 1_93D over 1_93C.
    {
        const std::vector<std::string> names = nextpi::parse_release_listing(
            "<a href=\"NextPi-1_93C.tar.gz\">x</a> <a href=\"NextPi-1_93C.tar.gz.md5\">m</a>"
            "<a href=\"NextPi-1_93D.tar.gz\">NextPi-1_93D.tar.gz</a> <a href=\"NextPi-1_100.tar.gz\">");
        const bool listing = names == std::vector<std::string>({"1_93C", "1_93D", "1_100"});
        const bool order = nextpi::compare_release("1_100", "1_93D") > 0
                        && nextpi::compare_release("1_93D", "1_93C") > 0
                        && nextpi::compare_release("1_93D", "1_93D") == 0
                        && nextpi::compare_release("1_9", "1_10") < 0;
        std::string joined;
        for (const std::string& n : names) joined += n + " ";
        check("PI-10",
              "NextPi's release list is read from the mirror's NextPi-<name>.tar.gz "
              "links, once each, and release names order numerically (1_100 after 1_93D)",
              listing && order, fmt("listed [%s] order=%d", joined.c_str(), order ? 1 : 0));
    }

    // ── PI-11 — the tar stream: the image entry is found by suffix through a pax
    // path record, its size read from a GNU base-256 field, and written out
    // byte for byte; an archive without such an entry is an error.
    {
        FakeMirror mirror("tar");
        const fs::path archive = mirror.dir / "a.tar.gz";
        const fs::path out = mirror.dir / "out.img";
        std::string err, err_missing;
        const bool wrote = write_fake_release(archive.string(), "T", disk_a);
        const bool ok = wrote && nextpi::extract_tar_gz_entry(archive.string(), ".img", out.string(), {}, err);
        const bool same = ok && slurp(out) == std::string(disk_a.begin(), disk_a.end());
        const bool missing = !nextpi::extract_tar_gz_entry(archive.string(), ".iso", out.string(), {}, err_missing)
                           && err_missing.find(".iso") != std::string::npos;
        check("PI-11",
              "the NextPi archive reader finds the image through a pax path record, "
              "reads its GNU base-256 size, and writes it byte for byte; a missing "
              "entry is an error",
              ok && same && missing,
              fmt("ok=%d (%s) same=%d missing='%s'", ok ? 1 : 0, err.c_str(), same ? 1 : 0,
                  err_missing.c_str()));
    }

    // ── PI-12 — FIRST USE: asked once, the checksum and archive fetched, the
    // image and both boot files installed, the release recorded, the archive
    // deleted. The next run finds it ready: no question, no network.
    {
        FakeMirror mirror("first");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "nextpi";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const bool installed = r1.status == sdcard::ProvisionStatus::Ok && r1.release == "1_93D"
            && slurp(dir / "nextpi.img") == std::string(disk_a.begin(), disk_a.end())
            && slurp(dir / "boot" / "kernel.img") == "kernel image a"
            && slurp(dir / "boot" / "bcm2708-rpi-zero.dtb") == "device tree a"
            && nextpi::prepared_release(dir.string()) == "1_93D"
            && !fs::exists(dir / "NextPi-1_93D.tar.gz");
        const std::size_t fetched_first = mirror.fetched.size();
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const bool second_quiet = r2.status == sdcard::ProvisionStatus::Ok && confirms == 1
                                && mirror.fetched.size() == fetched_first;
        check("PI-12",
              "first use of NextPi asks once, downloads the default release with "
              "its MD5, installs the image and the two boot files from its FAT32 "
              "partition and deletes the archive; the next run asks nothing and "
              "fetches nothing",
              fixture && installed && fetched_first == 2 && second_quiet,
              fmt("fixture=%d installed=%d (%s) fetched=%zu (want 2) second_quiet=%d confirms=%d",
                  fixture ? 1 : 0, installed ? 1 : 0, r1.error.c_str(), fetched_first,
                  second_quiet ? 1 : 0, confirms));
    }

    // ── PI-13 — the refusals: declining downloads nothing and installs nothing;
    // an archive whose MD5 does not match is deleted and nothing is installed.
    {
        FakeMirror mirror("refuse");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir_no = mirror.dir / "declined";
        int confirms = 0;
        const nextpi::ProvisionResult declined =
            nextpi::provision(nextpi_options(mirror, dir_no.string(), "", confirms, false));
        const bool declined_ok = declined.status == sdcard::ProvisionStatus::Declined
                               && mirror.fetched.empty() && nextpi::prepared_release(dir_no.string()).empty();

        std::ofstream(mirror.dir / "NextPi-1_93D.tar.gz.md5") << "00000000000000000000000000000000\n";
        const fs::path dir_bad = mirror.dir / "corrupt";
        const nextpi::ProvisionResult corrupt =
            nextpi::provision(nextpi_options(mirror, dir_bad.string(), "", confirms));
        const bool corrupt_ok = corrupt.status == sdcard::ProvisionStatus::Failed
                              && corrupt.error.find("MD5") != std::string::npos
                              && !fs::exists(dir_bad / "NextPi-1_93D.tar.gz")
                              && nextpi::prepared_release(dir_bad.string()).empty();
        check("PI-13",
              "declining the NextPi download fetches and installs nothing; a download "
              "whose MD5 does not match is deleted and nothing is installed",
              fixture && declined_ok && corrupt_ok,
              fmt("fixture=%d declined=%d corrupt=%d ('%s')", fixture ? 1 : 0,
                  declined_ok ? 1 : 0, corrupt_ok ? 1 : 0, corrupt.error.c_str()));
    }

    // ── PI-14 — CHANGING RELEASE, and "latest". A directory holding another
    // release is replaced after asking, and its overlay (made over the old
    // image) discarded. "latest" picks the newest listed release; with the
    // mirror unreachable it keeps the installed one, with a warning, asking
    // nothing.
    {
        FakeMirror mirror("change");
        const bool fixture = mirror.add_release("1_93D", disk_a) && mirror.add_release("1_100", disk_b);
        const fs::path dir = mirror.dir / "nextpi";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "1_93D", confirms));
        std::ofstream(dir / "overlay.qcow2") << "old overlay";
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool upgraded = r1.status == sdcard::ProvisionStatus::Ok
            && r2.status == sdcard::ProvisionStatus::Ok && r2.release == "1_100" && confirms == 2
            && slurp(dir / "boot" / "kernel.img") == "kernel image b" && !fs::exists(dir / "overlay.qcow2");
        mirror.offline = true;
        const nextpi::ProvisionResult r3 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool offline_kept = r3.status == sdcard::ProvisionStatus::Ok && r3.release == "1_100"
                                && !r3.warning.empty() && confirms == 2;
        check("PI-14",
              "the NextPi provisioner replaces a directory holding another release after asking, "
              "discarding its overlay; \"latest\" installs the newest listed release, and "
              "with the mirror unreachable keeps the installed one with a warning",
              fixture && upgraded && offline_kept,
              fmt("fixture=%d upgraded=%d (%s / %s, confirms=%d) offline_kept=%d ('%s')",
                  fixture ? 1 : 0, upgraded ? 1 : 0, r1.error.c_str(), r2.release.c_str(),
                  confirms, offline_kept ? 1 : 0, r3.warning.c_str()));
    }
}

static void test_nextpi_review_rows() {
    set_group("PI");
    namespace fs = std::filesystem;

    // ── PI-15 — THE REPLAY GATE, the JOY-20 posture for the Pi. While a rewind
    // or an RZX playback re-executes frames, Emulator::service_pi_uart_frame
    // holds the link inert: a replayed transmission does not reach the Pi a
    // second time, and the Pi's bytes are not read (they stay in the pipe for
    // the timeline that resumes). Afterwards both flow again.
    {
        TempFifoCable cable("pi-replay");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        pi_transmit_frame(emu, 0x30, 0xD1);
        const std::vector<uint8_t> live_before = cable.drain();

        emu.set_replay_mode(true);
        cable.send({0xE1, 0xE2});
        pi_transmit_frame(emu, 0x30, 0xD2);
        const std::vector<uint8_t> during = cable.drain();
        const PiUartDevice* pi = emu.pi_uart();
        const bool not_read = pi && pi->link().received() == 0;

        emu.set_replay_mode(false);
        std::vector<uint8_t> heard;
        for (int f = 0; f < 3; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            heard.insert(heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xD3);
        const std::vector<uint8_t> live_after = cable.drain();
        check("PI-15",
              "during a rewind/RZX replay the NextPi link is inert: the guest's replayed byte "
              "does not reach the Pi and the Pi's bytes are not consumed; afterwards they "
              "arrive and the guest is heard again",
              attached && live_before == std::vector<uint8_t>({0xD1}) && during.empty() && not_read &&
                  heard == std::vector<uint8_t>({0xE1, 0xE2}) && live_after == std::vector<uint8_t>({0xD3}),
              fmt("before=[%s] during=[%s] (want empty) not_read=%d heard=[%s] (want E1 E2) after=[%s]",
                  bytes_hex(live_before).c_str(), bytes_hex(during).c_str(), not_read ? 1 : 0,
                  bytes_hex(heard).c_str(), bytes_hex(live_after).c_str()));
    }

    // ── PI-16 — the warm-start recording machine (Emulator::warm_start_boot_config)
    // must not open the NextPi FIFOs: a second reader would steal the Pi's bytes.
    {
        EmulatorConfig live;
        live.type            = MachineType::ZXN_ISSUE2;
        live.sd_card_image   = "card.img";
        live.pi_uart_fifo_rx = "/tmp/x/uart.out";
        live.pi_uart_fifo_tx = "/tmp/x/uart.in";
        const EmulatorConfig b = Emulator::warm_start_boot_config(live);
        check("PI-16",
              "the warm-start recording boot gets no NextPi link (both FIFO paths cleared); the "
              "machine itself is kept",
              b.pi_uart_fifo_rx.empty() && b.pi_uart_fifo_tx.empty() && b.sd_card_image == "card.img",
              fmt("rx='%s' tx='%s'", b.pi_uart_fifo_rx.c_str(), b.pi_uart_fifo_tx.c_str()));
    }

    // ── PI-17 — SIGKILL ESCALATION: a QEMU that ignores SIGTERM is killed —
    // with its watchdog — once the grace period is over, and stop() returns.
    {
        FakeNextPi fake("stubborn");
        PiQemu::Spec spec;
        spec.dir           = fake.dir();
        spec.qemu_binary   = fake.bin("qemu-stubborn");
        spec.audio         = "none";
        spec.stop_grace_ms = 300;
        auto qemu = std::make_unique<PiQemu>();
        std::string error;
        const bool started = fake.ok() && qemu->start(spec, error);
        const int child = fake.child_pid();
        const auto t0 = std::chrono::steady_clock::now();
        qemu.reset();
        const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count());
        const bool killed = child > 0 && gone_within(child, 1000);
        check("PI-17",
              "a QEMU that ignores SIGTERM is SIGKILLed with its watchdog once the stop grace "
              "period is over, and stop() returns",
              started && killed && ms >= 300 && ms < 3000,
              fmt("started=%d (%s) child=%d killed=%d stop took %ld ms (want 300..3000)",
                  started ? 1 : 0, error.c_str(), child, killed ? 1 : 0, ms));
    }

    // ── PI-18 / PI-19 — THE CHILD. Its locale is C whatever jnext's is (set
    // in the child only), and it inherits none of jnext's descriptors: a file
    // jnext holds open at fd 57 without close-on-exec is not open in QEMU.
    {
        FakeNextPi fake("child");
        const char* old_lang = std::getenv("LANG");
        const char* old_lc   = std::getenv("LC_ALL");
        const std::string saved_lang = old_lang ? old_lang : "";
        const std::string saved_lc   = old_lc ? old_lc : "";
        jtp::set_env("LANG", "es_ES.UTF-8");
        jtp::set_env("LC_ALL", "es_ES.UTF-8");
        const int leak = ::open(fake.dir().c_str(), O_RDONLY);
        const bool leak_ok = leak >= 0 && ::dup2(leak, 57) == 57;
        if (leak >= 0 && leak != 57) ::close(leak);

        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error) && fake.child_pid() > 0;
        const bool jnext_locale_kept = std::string(std::getenv("LANG")) == "es_ES.UTF-8";
        if (old_lang) jtp::set_env("LANG", saved_lang.c_str()); else jtp::unset_env("LANG");
        if (old_lc) jtp::set_env("LC_ALL", saved_lc.c_str()); else jtp::unset_env("LC_ALL");
        if (leak_ok) ::close(57);

        // Exactly LANG and LC_ALL are the promise, each present once and C.
        // Other variables may legitimately carry jnext's locale (a GNOME
        // session exports GDM_LANG=es_ES.UTF-8), so they are not searched.
        const std::string env = "\n" + fake.file("env");
        auto count = [&env](const char* line) {
            std::size_t n = 0;
            for (std::size_t at = env.find(line); at != std::string::npos; at = env.find(line, at + 1)) ++n;
            return n;
        };
        const bool env_c = count("\nLANG=") == 1 && count("\nLANG=C\n") == 1 &&
                           count("\nLC_ALL=") == 1 && count("\nLC_ALL=C\n") == 1;
        check("PI-18",
              "QEMU runs with LANG=C and LC_ALL=C set in its own environment, whatever jnext's "
              "locale is, and jnext's own environment is left alone",
              started && env_c && jnext_locale_kept,
              fmt("started=%d (%s) env_c=%d jnext_kept=%d", started ? 1 : 0, error.c_str(),
                  env_c ? 1 : 0, jnext_locale_kept ? 1 : 0));

        // fd 3 is the watchdog's end of its pipe, which the watchdog closes
        // for QEMU (`3<&-`): QEMU gets none of jnext's descriptors, that pipe
        // included (R3-3).
        const std::string fds = "\n" + fake.file("fds");
        const bool listed = !fake.file("fds").empty();
        const std::string fd3 = fake.file("fd3");
        check("PI-19",
              "QEMU inherits none of jnext's descriptors: a file jnext holds at fd 57 without "
              "close-on-exec is not open in the child, nor is the watchdog's pipe at fd 3",
              started && leak_ok && listed && fds.find("\n57\n") == std::string::npos && fd3 == "closed\n",
              fmt("started=%d leak_ok=%d listed=%d fd 3 %s child fds:%s", started ? 1 : 0, leak_ok ? 1 : 0,
                  listed ? 1 : 0, fd3.c_str(), fake.file("fds").c_str()));
    }

    // ── PI-20 — A KILLED jnext DOES NOT ORPHAN QEMU. A process holding a
    // running PiQemu is SIGKILLed (no destructor runs); its watchdog sees the
    // pipe close and stops QEMU, which would otherwise keep the overlay locked.
    {
        FakeNextPi fake("orphan");
        int child_qemu = 0;
        bool held = false;
#ifndef _WIN32
        const pid_t holder = ::fork();
        if (holder == 0) {
            PiQemu::Spec spec;
            spec.dir         = fake.dir();
            spec.qemu_binary = fake.bin("qemu-system-arm");
            spec.audio       = "none";
            auto* q = new PiQemu();                 // never destroyed: SIGKILLed first
            std::string e;
            if (!q->start(spec, e)) ::_exit(2);
            for (;;) ::pause();
        }
        if (holder > 0) {
            child_qemu = fake.ok() ? fake.child_pid() : 0;
            held = child_qemu > 0 && process_alive(child_qemu);
            ::kill(holder, SIGKILL);
            int st = 0;
            ::waitpid(holder, &st, 0);
        }
#endif
        const bool stopped = child_qemu > 0 && gone_within(child_qemu, 3000);
        check("PI-20",
              "when the process running NextPi is SIGKILLed, the watchdog stops QEMU instead of "
              "leaving it orphaned with the overlay locked",
              held && stopped,
              fmt("qemu pid=%d running while held=%d stopped after the kill=%d", child_qemu,
                  held ? 1 : 0, stopped ? 1 : 0));
    }

    // ── PI-21 — the free-space check: with less free space than unpacking needs,
    // the download is refused before anything is fetched.
    {
        FakeMirror mirror("space");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, (mirror.dir / "np").string(), "", confirms);
        o.space_needed = std::numeric_limits<uint64_t>::max();
        const nextpi::ProvisionResult r = nextpi::provision(o);
        check("PI-21",
              "NextPi is not downloaded when the directory lacks the free space unpacking needs: "
              "refused with the amounts, nothing fetched",
              fixture && r.status == sdcard::ProvisionStatus::Failed &&
                  r.error.find("free") != std::string::npos && mirror.fetched.empty(),
              fmt("status=%d error='%s' fetched=%zu", static_cast<int>(r.status), r.error.c_str(),
                  mirror.fetched.size()));
    }

    // ── PI-22 — long names: a GNU 'L' record, and a POSIX ustar prefix, each
    // supply the directory part a suffix match needs (neither short name has it).
    {
        FakeMirror mirror("names");
        const std::string data = "image bytes";
        std::string gnu = tar_entry("././@LongLink", "NextPi-T/very/deep/name.img", 'L');
        gnu += tar_entry("short-name", data);
        std::string ustar_hdr = tar_header("x.img", data.size(), '0');
        std::memcpy(&ustar_hdr[257], "ustar\0" "00", 8);           // POSIX, not GNU
        std::memcpy(&ustar_hdr[345], "NextPi-T/dir", 12);         // the prefix field
        std::memset(&ustar_hdr[148], ' ', 8);
        unsigned sum = 0;
        for (unsigned char c : ustar_hdr) sum += c;
        std::snprintf(&ustar_hdr[148], 8, "%06o", sum);
        std::string ustar = ustar_hdr + data;
        ustar.resize((ustar.size() + 511) / 512 * 512, '\0');
        const fs::path a_gnu = mirror.dir / "gnu.tar.gz", a_ustar = mirror.dir / "ustar.tar.gz";
        const fs::path out = mirror.dir / "out";
        std::string e1, e2;
        const bool ok_gnu = write_tar_gz(a_gnu.string(), gnu) &&
            nextpi::extract_tar_gz_entry(a_gnu.string(), "deep/name.img", out.string(), {}, e1) &&
            slurp(out) == data;
        const bool ok_ustar = write_tar_gz(a_ustar.string(), ustar) &&
            nextpi::extract_tar_gz_entry(a_ustar.string(), "dir/x.img", out.string(), {}, e2) &&
            slurp(out) == data;
        check("PI-22",
              "the NextPi archive reader takes an entry's full name from a GNU long-name record "
              "and from a POSIX ustar prefix",
              ok_gnu && ok_ustar, fmt("gnu=%d (%s) ustar=%d (%s)", ok_gnu ? 1 : 0, e1.c_str(),
                                      ok_ustar ? 1 : 0, e2.c_str()));
    }

    // ── PI-23 — A MALFORMED ARCHIVE IS AN ERROR, NOT A CRASH. Each bound has
    // its own case, and each case must fail with THAT bound's message: a
    // long-name record claiming ~2^64 bytes and an entry of 2^40+1 hit the
    // entry-size bound, an `x` header of 2^40 the metadata bound, and a pax
    // `size=` record of 2^40+1 the pax size bound. No exception escapes. (The
    // fixture's tag must not contain any of the words searched for: it is in
    // every archive path, so in every message.)
    {
        FakeMirror mirror("bounds");
        const fs::path out = mirror.dir / "out";
        auto try_one = [&](const char* name, const std::string& body, const char* want,
                           std::string& why) {
            const fs::path a = mirror.dir / name;
            if (!write_tar_gz(a.string(), body)) { why = "fixture"; return false; }
            try {
                return !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                       why.find(want) != std::string::npos;
            } catch (const std::exception& ex) {
                why = std::string("threw ") + ex.what();
                return false;
            }
        };
        std::string huge_l = tar_header("././@LongLink", 0, 'L');
        huge_l[124] = static_cast<char>(0x80);
        for (int i = 125; i < 136; ++i) huge_l[i] = static_cast<char>(0xFF);
        huge_l[135] = static_cast<char>(0x00);
        std::string big_x = tar_header("PaxHeaders/x", 1ull << 40, 'x', true);
        std::string big_file = tar_header("big.img", (1ull << 40) + 1, '0', true);
        std::string pax_size = tar_entry("PaxHeaders/big.img", pax_record("size", "1099511627777"), 'x') +
                               tar_entry("big.img", "");
        std::string w1, w2, w3, w4;
        const bool l_ok = try_one("l.tar.gz", huge_l, "an entry claims", w1);
        const bool x_ok = try_one("x.tar.gz", big_x, "name/metadata record", w2);
        const bool f_ok = try_one("f.tar.gz", big_file, "an entry claims", w3);
        const bool p_ok = try_one("p.tar.gz", pax_size, "a pax size of", w4);
        check("PI-23",
              "a NextPi archive with an absurd long-name, pax header, entry or pax size= record "
              "fails with that bound's \"malformed\" message instead of throwing (no crash after a "
              "6 GB download)",
              l_ok && x_ok && f_ok && p_ok,
              fmt("L: %d (%s) x: %d (%s) entry: %d (%s) pax size: %d (%s)", l_ok ? 1 : 0, w1.c_str(),
                  x_ok ? 1 : 0, w2.c_str(), f_ok ? 1 : 0, w3.c_str(), p_ok ? 1 : 0, w4.c_str()));
    }

    // ── PI-24 — a FAILED UPGRADE KEEPS THE OLD RELEASE. 1_93D is installed;
    // "latest" finds 1_100 whose archive (with a valid MD5) has no image: the
    // upgrade fails, and 1_93D is still prepared, marker and files intact.
    {
        FakeMirror mirror("keep");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("kernel old", "dtb old"));
        const fs::path broken = mirror.dir / "NextPi-1_100.tar.gz";
        const bool broken_ok = write_tar_gz(broken.string(), tar_entry("NextPi-1_100/readme.txt", "no image"));
        std::ofstream(broken.string() + ".md5") << nextpi::md5_file(broken.string()) << "\n";
        mirror.listing += "<a href=\"NextPi-1_100.tar.gz\">x</a>\n";
        const fs::path dir = mirror.dir / "np";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool kept = r1.status == sdcard::ProvisionStatus::Ok && r2.status == sdcard::ProvisionStatus::Failed &&
                          nextpi::prepared_release(dir.string()) == "1_93D" &&
                          slurp(dir / "boot" / "kernel.img") == "kernel old" &&
                          !fs::exists(dir / "nextpi.img.part") && !fs::exists(dir / "boot.part");
        check("PI-24",
              "a NextPi upgrade that fails while unpacking leaves the installed release prepared "
              "and intact, with no partial files",
              fixture && broken_ok && kept,
              fmt("fixture=%d r1=%d r2=%d ('%s') prepared='%s'", fixture && broken_ok ? 1 : 0,
                  static_cast<int>(r1.status), static_cast<int>(r2.status), r2.error.c_str(),
                  nextpi::prepared_release(dir.string()).c_str()));
    }

    // ── PI-25 — the release name from Preferences goes into a file name and a
    // URL, so anything but a mirror-style name is refused before either.
    {
        FakeMirror mirror("names-check");
        int confirms = 0;
        bool all_refused = true;
        std::string detail;
        for (const char* bad : {"../escape", "a/b", "a b", ".hidden", "x?y=1"}) {
            const nextpi::ProvisionResult r =
                nextpi::provision(nextpi_options(mirror, (mirror.dir / "np").string(), bad, confirms));
            if (r.status != sdcard::ProvisionStatus::Failed || r.error.find("release name") == std::string::npos)
                all_refused = false;
            detail += std::string(bad) + "->" + std::to_string(static_cast<int>(r.status)) + " ";
        }
        const bool good = nextpi::valid_release_name("1_93D") && nextpi::valid_release_name("2.0-beta");
        check("PI-25",
              "a NextPi release name with a path separator, space, leading dot or URL syntax is "
              "refused before anything is asked or fetched; real names pass",
              all_refused && good && confirms == 0 && mirror.fetched.empty(),
              fmt("%sconfirms=%d fetched=%zu good=%d", detail.c_str(), confirms, mirror.fetched.size(),
                  good ? 1 : 0));
    }

    // ── PI-26 — a FAILED START CLEANS UP what it made: the overlay it created
    // is removed (it would otherwise be reused, half-made), qemu.log is kept
    // because the error points at it.
    {
        FakeNextPi fake("cleanup");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-fails");
        PiQemu q;
        std::string error;
        const bool refused = fake.ok() && !q.start(spec, error);
        const bool overlay_gone = !fs::exists(fs::path(fake.dir()) / "overlay.qcow2");
        const bool log_kept = fs::exists(fs::path(fake.dir()) / "qemu.log") &&
                              error.find("qemu.log") != std::string::npos;
        check("PI-26",
              "a NextPi start that fails removes the overlay it created and keeps qemu.log, "
              "which its error names",
              refused && overlay_gone && log_kept,
              fmt("refused=%d (%s) overlay_gone=%d log_kept=%d", refused ? 1 : 0, error.c_str(),
                  overlay_gone ? 1 : 0, log_kept ? 1 : 0));
    }

    // ── PI-27 — THE START POLICY (main.cpp, through nextpi::start_outcome):
    // running is running; declining starts jnext without NextPi either way; a
    // failure is an error only when --nextpi asked for it, and from Preferences
    // a warning that lets jnext start.
    {
        using nextpi::StartOutcome;
        const auto Ok = sdcard::ProvisionStatus::Ok, No = sdcard::ProvisionStatus::Declined,
                   Bad = sdcard::ProvisionStatus::Failed;
        const bool table =
            nextpi::start_outcome(true, Ok, true) == StartOutcome::Started &&
            nextpi::start_outcome(false, Ok, true) == StartOutcome::Started &&
            nextpi::start_outcome(true, No, false) == StartOutcome::Declined &&
            nextpi::start_outcome(false, No, false) == StartOutcome::Declined &&
            nextpi::start_outcome(true, Bad, false) == StartOutcome::Exit &&
            nextpi::start_outcome(true, Ok, false) == StartOutcome::Exit &&
            nextpi::start_outcome(false, Bad, false) == StartOutcome::WarnAndContinue &&
            nextpi::start_outcome(false, Ok, false) == StartOutcome::WarnAndContinue;
        check("PI-27",
              "NextPi's start policy: a failure exits only when --nextpi asked for it (from "
              "Preferences it warns and continues); declining continues either way",
              table, "");
    }
}

static void test_nextpi_review2_rows() {
    set_group("PI");
    namespace fs = std::filesystem;

    // ── PI-28 — THE INSTALL IS CRASH-SAFE (R2-3). The `release` marker is what
    // makes a directory "prepared", so it is removed BEFORE anything is
    // replaced and written LAST, through release.part and a rename. Two
    // installs are made to fail part-way, with obstacles that stop root too:
    //   a. the image cannot be put in place (a directory holds its name): the
    //      old marker must already be gone;
    //   b. the marker cannot be written (a directory holds release.part): the
    //      new release is in place, but the directory must not claim it.
    // Either way the next start re-provisions instead of booting a mixture.
    {
        FakeMirror mirror("swap");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        int confirms = 0;

        const fs::path dir_a = mirror.dir / "a";
        std::error_code ec;
        fs::create_directories(dir_a / "nextpi.img" / "in-the-way", ec);
        std::ofstream(dir_a / "release") << "1_92\n";
        const nextpi::ProvisionResult ra = nextpi::provision(nextpi_options(mirror, dir_a.string(), "", confirms));
        const bool a_ok = ra.status == sdcard::ProvisionStatus::Failed &&
                          ra.error.find("cannot install") != std::string::npos &&
                          !fs::exists(dir_a / "release");

        const fs::path dir_b = mirror.dir / "b";
        fs::create_directories(dir_b / "release.part" / "in-the-way", ec);
        const nextpi::ProvisionResult rb = nextpi::provision(nextpi_options(mirror, dir_b.string(), "", confirms));
        const bool b_ok = rb.status == sdcard::ProvisionStatus::Failed &&
                          fs::is_regular_file(dir_b / "nextpi.img") && !fs::exists(dir_b / "release") &&
                          nextpi::prepared_release(dir_b.string()).empty();
        check("PI-28",
              "a NextPi install that fails part-way leaves no release marker: it is removed before "
              "the image is replaced and written last through release.part, so the directory is "
              "never taken as prepared",
              fixture && a_ok && b_ok,
              fmt("fixture=%d a=%d ('%s', marker %s) b=%d ('%s', marker %s)", fixture ? 1 : 0,
                  a_ok ? 1 : 0, ra.error.c_str(), fs::exists(dir_a / "release") ? "kept" : "gone",
                  b_ok ? 1 : 0, rb.error.c_str(), fs::exists(dir_b / "release") ? "written" : "absent"));
    }

    // ── PI-29 — NAMES FROM THE MIRROR ARE CHECKED (R2-4). For "latest" the
    // release name comes from the mirror's listing, not from the user, and the
    // listing is the only place it is checked: a leading dot or dash, or more
    // than 64 characters, is not a release. The 65-digit name would win
    // "latest" numerically, so the provisioner must never fetch it.
    {
        const std::string long_name(65, '9');
        const std::string html = "<a href=\"NextPi-.hidden.tar.gz\">x</a> <a href=\"NextPi--rf.tar.gz\">x</a> "
                                 "<a href=\"NextPi-" + long_name + ".tar.gz\">x</a> "
                                 "<a href=\"NextPi-1_93D.tar.gz\">x</a>";
        const std::vector<std::string> names = nextpi::parse_release_listing(html);
        const bool listed_ok = names == std::vector<std::string>({"1_93D"});

        FakeMirror mirror("listing");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        mirror.listing += "<a href=\"NextPi-" + long_name + ".tar.gz\">x</a>\n";
        int confirms = 0;
        const fs::path dir = mirror.dir / "np";
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        bool fetched_bad = false;
        for (const std::string& u : mirror.fetched)
            if (u.find(long_name) != std::string::npos) fetched_bad = true;
        std::string joined;
        for (const std::string& n : names) joined += n + " ";
        check("PI-29",
              "release names from the mirror's listing are checked like typed ones: a leading dot "
              "or dash or a 65-character name is not listed, and \"latest\" never fetches one",
              fixture && listed_ok && r.status == sdcard::ProvisionStatus::Ok && r.release == "1_93D" &&
                  !fetched_bad,
              fmt("listed [%s] (want 1_93D) latest=%s status=%d fetched_bad=%d", joined.c_str(),
                  r.release.c_str(), static_cast<int>(r.status), fetched_bad ? 1 : 0));
    }

    // ── PI-30 — NO TEMPORARY DIRECTORY (R2-6). With $TMPDIR naming no
    // directory, start() fails with an error that says so — it used to throw
    // std::filesystem_error out of jnext (SIGABRT) — and leaves nothing behind.
    {
        FakeNextPi fake("tmpdir");
        const char* old = std::getenv("TMPDIR");
        const std::string saved = old ? old : "";
        jtp::set_env("TMPDIR", "/nonexistent-jnext-tmpdir");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        bool refused = false, threw = false;
        std::string error;
        {
            PiQemu q;
            try {
                refused = !q.start(spec, error) && !q.running();
            } catch (const std::exception& ex) {
                threw = true;
                error = ex.what();
            }
        }
        if (old) jtp::set_env("TMPDIR", saved.c_str()); else jtp::unset_env("TMPDIR");
        check("PI-30",
              "with $TMPDIR naming no directory, starting NextPi fails with an error naming the "
              "temporary directory instead of throwing, and nothing is left running",
              fake.ok() && refused && !threw && error.find("temporary directory") != std::string::npos &&
                  !fs::exists(fs::path(fake.dir()) / "overlay.qcow2"),
              fmt("refused=%d threw=%d error='%s'", refused ? 1 : 0, threw ? 1 : 0, error.c_str()));
    }

    // ── PI-31 — A PAX RECORD CANNOT REACH OUTSIDE ITS HEADER (R2-8). Its length
    // is checked against what is left; 2^64-1 used to wrap `pos + len` and be
    // taken. Such a header is malformed, whatever the records after it say.
    {
        FakeMirror mirror("paxlen");
        const fs::path a = mirror.dir / "wrap.tar.gz", out = mirror.dir / "out";
        const std::string body = tar_entry("PaxHeaders/x", "18446744073709551615 path=EVIL.img\n", 'x') +
                                 tar_entry("data.bin", "payload");
        std::string why;
        bool refused = false;
        try {
            refused = write_tar_gz(a.string(), body) &&
                      !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                      why.find("pax record overruns") != std::string::npos;
        } catch (const std::exception& ex) {
            why = std::string("threw ") + ex.what();
        }
        check("PI-31",
              "a pax record whose length runs past its header (18446744073709551615) makes the "
              "archive malformed instead of wrapping and renaming the next entry",
              refused, fmt("error='%s'", why.c_str()));
    }

    // ── PI-39..42 — EACH WELL-FORMEDNESS CHECK OF THE PAX PARSER (R3-2). A pax
    // record is "<len> <key>=<value>\n", <len> counting the whole record. Each
    // archive below breaks exactly one rule, after a valid first record, and
    // must be refused as malformed; with that one check gone it would be read.
    {
        FakeMirror mirror("paxshape");
        const fs::path out = mirror.dir / "out";
        // `pad_newline_at` puts a '\n' at that offset of the header's data
        // block, in the zero padding past its end.
        auto refused = [&](const char* name, const std::string& records, std::string& why,
                           std::size_t pad_newline_at = 0) {
            const fs::path a = mirror.dir / name;
            std::string x = tar_entry("PaxHeaders/x", records, 'x');
            if (pad_newline_at) x[512 + pad_newline_at] = '\n';
            const std::string body = x + tar_entry("data.bin", "payload");
            try {
                return write_tar_gz(a.string(), body) &&
                       !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                       why.find("pax record overruns") != std::string::npos;
            } catch (const std::exception& ex) {
                why = std::string("threw ") + ex.what();
                return false;
            }
        };
        const std::string valid = pax_record("comment", "ok");

        // PI-39 — a record claiming more than what is left of the header: 99
        // bytes, and 2^64-1 (which wrapped `pos + len` in the original code).
        // The wrapped case is also refused by the space rule: a wrapped end
        // lies before `pos`, and so before the record's own space. And one
        // claiming 30 whose 30th byte, in the padding past the header, is a
        // '\n': only the length check stands between it and being read.
        std::string w1, w2, w6;
        const bool short_ok = refused("long.tar.gz", valid + "99 path=EVIL.img\n", w1);
        const bool wrap_ok  = refused("wrap.tar.gz", valid + "18446744073709551615 path=EVIL.img\n", w2);
        const bool pad_ok   = refused("pad.tar.gz", valid + "30 path=EVIL.img\n", w6, valid.size() + 29);
        check("PI-39",
              "a pax record after a valid one that claims more than is left of the header (99, "
              "2^64-1, or 30 with a '\\n' in the padding where it would end) makes the archive "
              "malformed",
              short_ok && wrap_ok && pad_ok,
              fmt("99: %d (%s) 2^64-1: %d (%s) padded: %d (%s)", short_ok ? 1 : 0, w1.c_str(),
                  wrap_ok ? 1 : 0, w2.c_str(), pad_ok ? 1 : 0, w6.c_str()));

        // PI-40 — the right length, but the record does not end on '\n'.
        std::string w3;
        const bool nl_ok = refused("nl.tar.gz", valid + "19 path=NextPi.imgX", w3);
        check("PI-40", "a pax record that does not end on '\\n' makes the archive malformed", nl_ok,
              fmt("error='%s'", w3.c_str()));

        // PI-41 — the length ends before the record's first space: "3\n\n" is
        // three bytes ending on '\n', and the space found is the next record's.
        std::string w4;
        const bool sp_ok = refused("space.tar.gz", valid + "3\n\n" + pax_record("path", "x.img"), w4);
        check("PI-41",
              "a pax record whose length ends before its first space (the space found belongs to the "
              "next record) makes the archive malformed",
              sp_ok, fmt("error='%s'", w4.c_str()));

        // PI-42 — no space at all in what is left.
        std::string w5;
        const bool nosp_ok = refused("nospace.tar.gz", valid + "4ab\n", w5);
        check("PI-42", "a pax record with no space after its length makes the archive malformed", nosp_ok,
              fmt("error='%s'", w5.c_str()));
    }

    // ── PI-43 — THE MARKER CANNOT BE REMOVED (R3-4). Removing `release` is the
    // install's first step; when it fails (here a non-empty directory holds
    // the name) the install stops there, before the image is put in place.
    {
        FakeMirror mirror("marker");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        fs::create_directories(dir / "release" / "in-the-way", ec);
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PI-43",
              "a NextPi install whose release marker cannot be removed fails at once, before the "
              "image is put in place",
              fixture && r.status == sdcard::ProvisionStatus::Failed &&
                  r.error.find("cannot install") != std::string::npos && !fs::exists(dir / "nextpi.img"),
              fmt("status=%d error='%s' image %s", static_cast<int>(r.status), r.error.c_str(),
                  fs::exists(dir / "nextpi.img") ? "installed" : "absent"));
    }

    // ── PI-44 — THE NAME LENGTH LIMIT, AT ITS BOUNDARY (R3-5): a release name
    // of 64 characters is accepted, one of 65 refused.
    {
        const bool at_64 = nextpi::valid_release_name(std::string(64, 'a'));
        const bool at_65 = nextpi::valid_release_name(std::string(65, 'a'));
        check("PI-44", "a NextPi release name of 64 characters is valid and one of 65 is not",
              at_64 && !at_65, fmt("64: %d 65: %d", at_64 ? 1 : 0, at_65 ? 1 : 0));
    }

    // ── PI-32 — A FAILED START KEEPS THE USER'S OVERLAY (R2-9). Only an overlay
    // the failing start created is removed: one that was there before holds
    // everything NextPi saved, and must survive.
    {
        FakeNextPi fake("keep-overlay");
        const fs::path overlay = fs::path(fake.dir()) / "overlay.qcow2";
        std::ofstream(overlay) << "NextPi's saved state";
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-fails");
        spec.audio       = "none";
        PiQemu q;
        std::string error;
        const bool refused = fake.ok() && !q.start(spec, error);
        check("PI-32",
              "a NextPi start that fails keeps an overlay that was already there (the user's saved "
              "NextPi state): only one the failing start created is removed",
              refused && slurp(overlay) == "NextPi's saved state",
              fmt("refused=%d overlay='%s'", refused ? 1 : 0, slurp(overlay).c_str()));
    }

    // ── PI-33 — THE CHILD ENVIRONMENT ITSELF (R2-10). PiQemu::child_environment
    // drops every LANG and LC_ALL entry before adding its own, so the child
    // gets each exactly once. (A shell in between, as PI-18 sees it, would
    // hide a duplicate; qemu-img is spawned directly and would not.)
    {
        const char* env[] = {"PATH=/bin", "LANG=es_ES.UTF-8", "LC_ALL=es_ES.UTF-8", "LANGUAGE=es",
                             "LC_ALL=fr_FR", nullptr};
        const std::vector<std::string> got = PiQemu::child_environment(env);
        const std::vector<std::string> want = {"PATH=/bin", "LANGUAGE=es", "LANG=C", "LC_ALL=C"};
        std::string joined;
        for (const std::string& v : got) joined += v + " ";
        check("PI-33",
              "a spawned NextPi child's environment is the parent's without any LANG or LC_ALL "
              "entry, then LANG=C and LC_ALL=C, each exactly once (LANGUAGE is not LANG)",
              got == want, fmt("got [%s]", joined.c_str()));
    }

    // ── PI-34 — THE WATCHDOG PASSES QEMU'S EXIT STATUS ON (R2-10). QEMU runs
    // under the watchdog shell; a QEMU that exits on its own with status 7
    // must show up as status 7, not as the shell's own 0.
    {
        FakeNextPi fake("status");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-exits-7");
        spec.audio       = "none";
        PiQemu q;
        std::string error;
        const bool started = fake.ok() && q.start(spec, error);
        int status = -1;
        for (int i = 0; i < 150 && (status = q.exit_status()) == -1; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
#ifndef _WIN32
        const bool seven = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 7;
#else
        const bool seven = false;
#endif
        check("PI-34",
              "when QEMU exits on its own, the status jnext reaps is QEMU's (7), passed on by the "
              "watchdog shell",
              started && seven, fmt("started=%d (%s) wait status=%d", started ? 1 : 0, error.c_str(), status));
    }

    // ── PI-35 — THE WATCHDOG PIPE IS CLOSE-ON-EXEC (R2-10). Every descriptor a
    // start leaves open in jnext — the watchdog pipe's write end — has
    // FD_CLOEXEC, so no other child jnext spawns (ffmpeg, a second QEMU) can
    // inherit it and keep the watchdog from ever seeing jnext go away.
    {
        FakeNextPi fake("cloexec");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        const std::set<int> before = open_fds();
        PiQemu q;
        std::string error;
        const bool started = fake.ok() && q.start(spec, error);
        std::string fresh;
        bool all_cloexec = true;
        int count = 0;
#ifndef _WIN32
        for (int fd : open_fds()) {
            if (before.count(fd)) continue;
            ++count;
            const bool ce = (::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0;
            all_cloexec = all_cloexec && ce;
            fresh += std::to_string(fd) + (ce ? "(cloexec) " : "(INHERITABLE) ");
        }
#endif
        check("PI-35",
              "every descriptor a NextPi start leaves open in jnext (the watchdog pipe's write "
              "end) is close-on-exec, so no other child can inherit it",
              started && count >= 1 && all_cloexec,
              fmt("started=%d (%s) new fds: %s", started ? 1 : 0, error.c_str(), fresh.c_str()));
    }

    // ── PI-36 — THE REPLAY GATE, RZX HALF (R2-11). PI-15 replays a rewind; an
    // RZX playback must hold the link inert the same way: the guest's byte
    // does not reach the Pi, the Pi's bytes are not read; afterwards both flow.
    {
        TempFifoCable cable("pi-rzx");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        RzxRecording rec;
        rec.frames.resize(50);
        emu.rzx_player().start(std::move(rec));
        cable.send({0xE1});
        pi_transmit_frame(emu, 0x30, 0xD2);
        const bool playing = emu.rzx_player().is_playing();
        const std::vector<uint8_t> during = cable.drain();
        const PiUartDevice* pi = emu.pi_uart();
        const bool not_read = pi && pi->link().received() == 0;
        emu.rzx_player().stop();

        std::vector<uint8_t> heard;
        for (int f = 0; f < 3; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            heard.insert(heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xD3);
        const std::vector<uint8_t> after = cable.drain();
        check("PI-36",
              "during an RZX playback the NextPi link is inert: the guest's byte does not reach the "
              "Pi and the Pi's bytes are not consumed; after it they arrive and the guest is heard",
              attached && playing && during.empty() && not_read &&
                  heard == std::vector<uint8_t>({0xE1}) && after == std::vector<uint8_t>({0xD3}),
              fmt("playing=%d during=[%s] (want empty) not_read=%d heard=[%s] (want E1) after=[%s]",
                  playing ? 1 : 0, bytes_hex(during).c_str(), not_read ? 1 : 0, bytes_hex(heard).c_str(),
                  bytes_hex(after).c_str()));
    }

    // ── PI-37 / PI-45 — THE CLOSE-ON-EXEC FALLBACK (R2-12, R3-6), for systems
    // where posix_spawn cannot close descriptors itself. It marks every open
    // descriptor from 3 up (fd 57) except the one to keep (fd 58), and leaves
    // stdin, stdout and stderr alone. PI-37 runs it as used, from the list of
    // open descriptors; PI-45 runs its last resort, the number walk, and
    // checks how far that walk goes for the limits sysconf can report.
    {
#ifndef _WIN32
        auto run = [](const std::vector<std::string>& lists, std::string& detail) {
            int p[2];
            if (::pipe(p) != 0) { detail = "pipe failed"; return false; }
            const bool fixture = ::dup2(p[0], 57) == 57 && ::dup2(p[1], 58) == 58;
            ::close(p[0]);
            ::close(p[1]);
            // fd 3, the walk's lower bound (R4-1), and the last number the
            // number walk reaches (R4-2), each holding a pipe end without
            // close-on-exec. Whatever this process had at fd 3 is set aside
            // and put back.
            const int saved3 = ::fcntl(3, F_DUPFD_CLOEXEC, 100);
            const int flags3 = ::fcntl(3, F_GETFD);
            // The walk's last number is usable only below RLIMIT_NOFILE: raise
            // the soft limit to it where the hard limit allows (on Linux
            // sysconf reports that limit itself, so `top` is exactly the last
            // number walked; macOS reports a larger one and caps the raise).
            const long walk = PiQemu::fd_walk_limit(::sysconf(_SC_OPEN_MAX));
            rlimit saved_nofile{};
            ::getrlimit(RLIMIT_NOFILE, &saved_nofile);
            rlimit raised = saved_nofile;
            if (raised.rlim_cur < static_cast<rlim_t>(walk)) {
                raised.rlim_cur = std::min<rlim_t>(static_cast<rlim_t>(walk), raised.rlim_max);
                if (::setrlimit(RLIMIT_NOFILE, &raised) != 0) ::getrlimit(RLIMIT_NOFILE, &raised);
            }
            // macOS also caps descriptors at kern.maxfilesperproc whatever
            // the rlimit says, so step down to the highest number that works.
            int top = static_cast<int>(std::min<rlim_t>(static_cast<rlim_t>(walk), raised.rlim_cur)) - 1;
            while (top > 1024 && ::dup2(58, top) != top) --top;
            const bool edges = ::dup2(58, 3) == 3 && ::dup2(58, top) == top;
            if (edges) {
                ::fcntl(3, F_SETFD, 0);
                ::fcntl(top, F_SETFD, 0);
            }
            int std_flags[3];
            for (int fd = 0; fd < 3; ++fd) {
                std_flags[fd] = ::fcntl(fd, F_GETFD);
                if (std_flags[fd] >= 0) ::fcntl(fd, F_SETFD, std_flags[fd] & ~FD_CLOEXEC);
            }
            bool marked = false, kept = false, std_alone = true, fd3 = false, at_top = false;
            if (fixture && edges) {
                PiQemu::mark_close_on_exec_except(58, lists);
                marked = (::fcntl(57, F_GETFD) & FD_CLOEXEC) != 0;
                kept   = (::fcntl(58, F_GETFD) & FD_CLOEXEC) == 0;
                fd3    = (::fcntl(3, F_GETFD) & FD_CLOEXEC) != 0;
                at_top = (::fcntl(top, F_GETFD) & FD_CLOEXEC) != 0;
                for (int fd = 0; fd < 3; ++fd)
                    if (std_flags[fd] >= 0 && (::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0) std_alone = false;
            }
            for (int fd = 0; fd < 3; ++fd)
                if (std_flags[fd] >= 0) ::fcntl(fd, F_SETFD, std_flags[fd]);
            ::close(top);
            ::setrlimit(RLIMIT_NOFILE, &saved_nofile);
            if (saved3 >= 0) {
                ::dup2(saved3, 3);
                ::fcntl(3, F_SETFD, flags3);   // dup2 cleared close-on-exec; restore what fd 3 had
                ::close(saved3);
            } else {
                ::close(3);
            }
            ::close(57);
            ::close(58);
            detail = fmt("fixture=%d edges=%d 57 marked=%d 58 kept=%d fd 3 marked=%d fd %d (top) marked=%d "
                         "0-2 untouched=%d", fixture ? 1 : 0, edges ? 1 : 0, marked ? 1 : 0, kept ? 1 : 0,
                         fd3 ? 1 : 0, top, at_top ? 1 : 0, std_alone ? 1 : 0);
            return fixture && edges && marked && kept && fd3 && at_top && std_alone;
        };
        std::string listed_detail, walked_detail;
        const bool listed = run({"/proc/self/fd", "/dev/fd"}, listed_detail);
        const bool walked = run({}, walked_detail);
#else
        const bool listed = false, walked = false;
        const std::string listed_detail, walked_detail;
#endif
        check("PI-37",
              "the close-on-exec fallback marks every open descriptor from 3 up (fd 3 itself, fd 57 "
              "and the walk's last number) except the one to keep (fd 58), and leaves stdin, stdout "
              "and stderr alone",
              listed, listed_detail);
        const bool limits = PiQemu::fd_walk_limit(-1) == 65536 && PiQemu::fd_walk_limit(0) == 65536 &&
                            PiQemu::fd_walk_limit(1024) == 1024 &&
                            PiQemu::fd_walk_limit(65536) == 65536 &&
                            PiQemu::fd_walk_limit(1000000000L) == 65536;
        check("PI-45",
              "the fallback's last resort, the number walk, marks the same descriptors, fd 3 and its "
              "last number included; it goes up to sysconf's limit capped at 65536, and to 65536 when "
              "the limit is indeterminate (-1) or 0",
              walked && limits, walked_detail + fmt(" limits=%d (-1 -> %ld, 1024 -> %ld, 10^9 -> %ld)",
                                                    limits ? 1 : 0, PiQemu::fd_walk_limit(-1),
                                                    PiQemu::fd_walk_limit(1024),
                                                    PiQemu::fd_walk_limit(1000000000L)));
    }

    // ── PI-46 — THE LIST IS WHAT IS USED (R4-3). Below 65536 the number walk
    // would mark the same descriptors, so the marks alone cannot tell that
    // the list was read: the walk reports which source it used. By default
    // /proc/self/fd where it exists (Linux), else /dev/fd (macOS); /dev/fd
    // when the first list cannot be read; the number walk ("") with none.
    {
        std::string detail;
        bool ok = false;
#ifndef _WIN32
        int p[2];
        if (::pipe(p) == 0 && ::dup2(p[0], 57) == 57) {
            ::close(p[0]);
            auto marked57 = [] { return (::fcntl(57, F_GETFD) & FD_CLOEXEC) != 0; };
            auto unmark57 = [] { ::fcntl(57, F_SETFD, ::fcntl(57, F_GETFD) & ~FD_CLOEXEC); };
            std::error_code ec;
            const std::string want_default =
                std::filesystem::is_directory("/proc/self/fd", ec) ? "/proc/self/fd" : "/dev/fd";
            const std::string by_default = PiQemu::mark_close_on_exec_except(-1);
            const bool m1 = marked57();
            unmark57();
            const std::string second = PiQemu::mark_close_on_exec_except(-1, {"/nonexistent-jnext-fd-list", "/dev/fd"});
            const bool m2 = marked57();
            unmark57();
            const std::string none = PiQemu::mark_close_on_exec_except(-1, {});
            const bool m3 = marked57();
            ok = by_default == want_default && m1 && second == "/dev/fd" && m2 && none.empty() && m3;
            detail = fmt("default='%s' (want '%s') marked=%d; second='%s' marked=%d; none='%s' marked=%d",
                         by_default.c_str(), want_default.c_str(), m1 ? 1 : 0, second.c_str(), m2 ? 1 : 0,
                         none.c_str(), m3 ? 1 : 0);
            ::close(57);
            ::close(p[1]);
        }
#endif
        check("PI-46",
              "the close-on-exec fallback reads /proc/self/fd where it exists, else /dev/fd, falls "
              "back to /dev/fd when the first list cannot be read, and walks the numbers only with "
              "no list; each marks fd 57",
              ok, detail);
    }

    // ── PI-47 — THE LIST LEAVES OUT ITS OWN DIRECTORY (R4-5). Reading /dev/fd
    // (or /proc/self/fd) opens a descriptor that the listing then names; it is
    // closed before anyone acts on the list, and its number may be reused by
    // then, so it must not be in the list. The directory gets the lowest free
    // number, which the row learns first; fd 57 is open and must be listed.
    // And the directory is CLOSED again (R5-1): the lowest free number is the
    // same after each read, and after each list-reading marking walk. A
    // directory left open would be a descriptor per start that, being left
    // off the list, nothing marks close-on-exec — so it would reach QEMU.
    {
        std::string detail;
        bool ok = false;
#ifndef _WIN32
        int p[2];
        if (::pipe(p) == 0 && ::dup2(p[0], 57) == 57) {
            ::close(p[0]);
            ok = true;
            auto lowest_free = [] {
                const int fd = ::open("/dev/null", O_RDONLY);
                ::close(fd);
                return fd;
            };
            for (const char* list : {"/dev/fd", "/proc/self/fd"}) {
                const int probe = lowest_free();   // the number the directory will get
                std::vector<int> fds;
                const bool read = PiQemu::open_descriptors(list, fds);
                if (!read && std::string(list) == "/proc/self/fd") continue;   // no /proc (macOS)
                const int after_read = lowest_free();
                PiQemu::mark_close_on_exec_except(-1, {list});
                const int after_mark = lowest_free();
                ::fcntl(57, F_SETFD, 0);
                const bool own_left_out = std::find(fds.begin(), fds.end(), probe) == fds.end();
                const bool has_57 = std::find(fds.begin(), fds.end(), 57) != fds.end();
                const bool closed = after_read == probe && after_mark == probe;
                ok = ok && read && own_left_out && has_57 && closed;
                detail += fmt("%s: read=%d own fd %d left out=%d 57 listed=%d lowest free before/after "
                              "read/after mark %d/%d/%d; ", list, read ? 1 : 0, probe, own_left_out ? 1 : 0,
                              has_57 ? 1 : 0, probe, after_read, after_mark);
            }
            ::close(57);
            ::close(p[1]);
        }
#endif
        check("PI-47",
              "the open-descriptor list read from /dev/fd (and /proc/self/fd where it exists) names "
              "the open descriptors (fd 57) but not the directory's own, which is closed again: no "
              "descriptor is left open by reading the list or by the walk that marks from it",
              ok, detail);
    }

    // ── PI-38 — WHETHER TO START NEXTPI (R2-5), main.cpp's decision through
    // nextpi::start_request. The command line wins either way; without it a
    // GUI session follows Preferences and a headless one starts nothing.
    // Only --nextpi makes a failure an error (asked_on_cli; PI-27).
    {
        using nextpi::start_request;
        auto is = [](nextpi::StartRequest r, bool wanted, bool asked) {
            return r.wanted == wanted && r.asked_on_cli == asked;
        };
        const bool table =
            is(start_request(true, true, true, false), true, true) &&     // GUI --nextpi, pref off
            is(start_request(true, true, false, true), false, false) &&   // GUI --no-nextpi, pref on
            is(start_request(true, false, false, true), true, false) &&   // GUI, pref on
            is(start_request(true, false, false, false), false, false) && // GUI, pref off
            is(start_request(false, true, true, false), true, true) &&    // headless --nextpi
            is(start_request(false, false, false, true), false, false) && // headless: no Preferences
            is(start_request(false, true, false, true), false, false);    // headless --no-nextpi
        check("PI-38",
              "whether NextPi starts: --nextpi and --no-nextpi win, otherwise a GUI session follows "
              "the [nextpi] preference and a headless one starts nothing; only --nextpi makes a "
              "failure an error",
              table, "");
    }
}

int main() {
    std::printf("UART integration: POSIX-only rows\n");
    std::printf("===============================================\n\n");

    test_joy_uart_cable_posix();
    std::printf("  Group: JOY — done\n");

    test_pi_uart_link();
    test_pi_qemu();
    test_nextpi_provisioner();
    test_nextpi_review_rows();
    test_nextpi_review2_rows();
    std::printf("  Group: PI — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());
    return g_fail > 0 ? 1 : 0;
}
