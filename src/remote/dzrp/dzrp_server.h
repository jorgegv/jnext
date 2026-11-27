#pragma once

// ---------------------------------------------------------------------------
// DzrpServer — the DeZog Remote Protocol adapter (GH #12, epic #276 package D).
//
// A `remote::Protocol` over the shared transport (package T, transport.h): it
// owns one `remote::Server`, parses DZRP frames from its `Connection`
// (dzrp_frame.h), executes each command synchronously against the backend
// (`jnext::dbg::Debugger`) and writes the reply. It never touches a socket and
// never touches `Emulator`: the transport is T's, the machine is the backend's.
//
// Design: doc/design/debug-subsystem/dzrp-frontend.md. The command table is §2,
// the session and loop model §4, the memory model on the wire §5.
//
// ── WIRING ─────────────────────────────────────────────────────────────────
//
// The loop owner (WP-5) does what every T adapter's owner does:
//
//     DzrpServer dzrp(debugger);
//     dzrp.server().open(cfg.debug_listen_address, port);
//     debugger.add_service(dzrp.server());
//
// and `Debugger::pump()` then drives it. The unit suite opens the same Server
// over T's `FakeListener` instead.
//
// ── THE COMMAND TABLE IS ONE TABLE ─────────────────────────────────────────
//
// `CMD_GET_SUPPORTED_COMMANDS`' bitfield is computed from the SAME table the
// dispatcher looks commands up in, so a command cannot be served and left
// unadvertised, or advertised and not served. The two legacy commands DZRP
// 2.2.0 removed (`CMD_WRITE_BANK`, `CMD_SET_BORDER`) are the one declared
// exception: served for 2.0/2.1 clients, flagged `legacy`, never advertised.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "debug/debugger.h"
#include "remote/dzrp/dzrp_frame.h"
#include "remote/dzrp/dzrp_protocol.h"
#include "remote/transport.h"

namespace jnext {
namespace remote {
namespace dzrp {

/// The most bytes `CMD_LOOPBACK` echoes (spec: "N is max. 8192").
constexpr std::size_t LOOPBACK_MAX_BYTES = 8192;

/// `CMD_READ_STATE`'s token prefix: the wire carries `"JNXB"` + a bookmark name
/// the backend holds (design §6), never the multi-megabyte snapshot.
constexpr char STATE_TOKEN_MAGIC[] = "JNXB";

class DzrpServer final : public Protocol, public jnext::dbg::Listener {
public:
    /// The clock the chunk timeout reads. Empty = `steady_clock::now`; the
    /// unit suite passes its own, so a 5 s timeout is tested without waiting.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit DzrpServer(jnext::dbg::Debugger& dbg, Clock clock = {});

    /// Unregisters its Server from the backend and ends a live session
    /// (`on_disconnect()` runs, so the client is detached). The `Debugger` must
    /// outlive this object.
    ~DzrpServer() override;

    DzrpServer(const DzrpServer&)            = delete;
    DzrpServer& operator=(const DzrpServer&) = delete;

    /// The transport this adapter serves on: `open()` it, `add_service()` it.
    Server& server() { return server_; }

    // ── remote::Protocol ──────────────────────────────────────────────────
    void                    on_connect(Connection& c) override;
    jnext::dbg::ServiceStep on_service(Connection& c) override;
    void                    on_notify(Connection& c) override;
    void                    on_disconnect() override;

    // ── dbg::Listener (SES-02) — installed for this client at CMD_INIT ──────
    void on_paused(const jnext::dbg::PausedInfo& info) override;
    void on_resumed(jnext::dbg::ClientId by) override;
    void on_reset(jnext::dbg::ResetKind kind) override;
    void on_frame_ended(std::uint32_t frame) override;
    void on_subscriptions_changed(jnext::dbg::EventKindMask kinds) override;
    void on_exit_requested(int code) override;
    void on_log(jnext::dbg::LogLevel level, const std::string& text) override;

private:
    /// One row of THE table (see the header banner).
    struct CommandDef {
        std::uint8_t id;
        const char*  name;
        /// Shortest payload the handler can act on; a shorter one is refused
        /// before the handler runs. 0 for a command whose reply carries its
        /// own error field, so the handler can report the fault in it.
        std::uint32_t min_len;
        /// Served but NOT advertised: removed in DZRP 2.2.0, kept for 2.0/2.1.
        bool legacy;
        /// Touches the machine, so it needs the session `CMD_INIT` opens: a
        /// mutation must be attributed to a client (`ClientId by`), and there
        /// is none before `CMD_INIT`. Refused until then.
        bool session;
        void (DzrpServer::*run)(const Command&);
    };
    static const CommandDef COMMANDS[];
    static const CommandDef* find_command(std::uint8_t id);

    /// `CMD_GET_SUPPORTED_COMMANDS`' bitfield, from `COMMANDS`: bit n set iff
    /// command n has a row and is not `legacy`. Little endian, as short as the
    /// highest set bit allows (the spec lets trailing bytes be omitted).
    static std::vector<std::uint8_t> supported_bitfield();

    void execute(const Command& cmd);
    void protocol_error(Connection& c, const std::string& why);
    void reply(std::uint8_t seq, const std::vector<std::uint8_t>& payload = {});
    void end_session();

    /// The `bank+1` byte for `addr`: the page mapped at its slot + 1 (F7).
    /// ROM cannot be named (§5.3): slot 0's ROM reports 0xFF (DeZog 3.7.4's
    /// 0xFE + 1), slot 1's reports 0 (0xFF + 1 overflows).
    std::uint8_t bank_byte(std::uint16_t addr) const;
    /// Queue an `NTF_PAUSE` for the next `on_notify()`.
    void queue_pause_ntf(std::uint8_t reason, std::uint16_t addr, const std::string& text);
    /// Remove whatever is left of the outstanding `CMD_CONTINUE`'s temporaries.
    void drop_temporaries();

    // Handlers — one per served command.
    void cmd_init(const Command& cmd);
    void cmd_close(const Command& cmd);
    void cmd_get_registers(const Command& cmd);
    void cmd_set_register(const Command& cmd);
    void cmd_write_bank(const Command& cmd);
    void cmd_read_mem(const Command& cmd);
    void cmd_write_mem(const Command& cmd);
    void cmd_set_slot(const Command& cmd);
    void cmd_get_tbblue_reg(const Command& cmd);
    void cmd_set_border(const Command& cmd);
    void cmd_loopback(const Command& cmd);
    void cmd_read_port(const Command& cmd);
    void cmd_write_port(const Command& cmd);
    void cmd_interrupt_on_off(const Command& cmd);
    void cmd_get_supported_commands(const Command& cmd);
    void cmd_read_bank_mem(const Command& cmd);
    void cmd_write_bank_mem(const Command& cmd);
    void cmd_set_nextregs(const Command& cmd);
    void cmd_read_mem_blocks(const Command& cmd);
    // WP-3
    void cmd_continue(const Command& cmd);
    void cmd_pause(const Command& cmd);
    void cmd_enable_break_on_interrupt(const Command& cmd);
    void cmd_add_breakpoint(const Command& cmd);
    void cmd_remove_breakpoint(const Command& cmd);
    // WP-4
    void cmd_get_sprites_palette(const Command& cmd);
    void cmd_get_sprites_clip_window_and_control(const Command& cmd);
    void cmd_get_sprites(const Command& cmd);
    void cmd_get_sprite_patterns(const Command& cmd);
    void cmd_add_watchpoint(const Command& cmd);
    void cmd_remove_watchpoint(const Command& cmd);
    void cmd_read_state(const Command& cmd);
    void cmd_write_state(const Command& cmd);

    jnext::dbg::Debugger& dbg_;
    Clock                 clock_;
    Server                server_;

    // Per-connection state.
    Connection*               conn_ = nullptr;  // valid inside on_service only
    FrameParser               parser_;
    std::vector<std::uint8_t> scratch_;

    // Per-session state: set by CMD_INIT, cleared by CMD_CLOSE or a disconnect.
    jnext::dbg::ClientId cid_ = jnext::dbg::CLIENT_NONE;
    std::uint8_t         client_version_[3] = {0, 0, 0};

    // WP-3 — breakpoints (DZRP id → backend subscription), the outstanding
    // CMD_CONTINUE's temporaries, break-on-interrupt, and what is owed.
    std::map<std::uint16_t, jnext::dbg::EventId> bps_;
    std::uint32_t                                next_bp_id_ = 1;  // 1..65535, never reused
    std::vector<jnext::dbg::EventId>             temps_;
    jnext::dbg::EventId                          int_ack_ = jnext::dbg::EVENT_NONE;
    bool continue_outstanding_ = false;  // one NTF_PAUSE owed per CMD_CONTINUE
    bool pause_owed_           = false;  // our CMD_PAUSE stopped a running machine
    std::optional<jnext::dbg::PausedInfo>  pending_pause_;
    std::vector<std::vector<std::uint8_t>> ntf_queue_;  // encoded frames

    // WP-4 — watchpoints (no ids on the wire: matched by the exact tuple) and
    // the state-token counter (the names themselves are the backend's).
    struct Watch {
        std::uint16_t       addr;
        std::uint8_t        bank1;
        std::uint16_t       size;
        std::uint8_t        access;
        jnext::dbg::EventId id;
    };
    std::vector<Watch>    wps_;
    std::uint32_t         next_token_ = 1;
    bool                  last_save_refused_mid_frame_ = false;
};

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
