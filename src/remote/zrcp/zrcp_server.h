#pragma once

// ---------------------------------------------------------------------------
// ZrcpServer — the ZEsarUX remote command protocol (ZRCP) adapter (GH #280,
// epic #276 package Z).
//
// A `remote::Protocol` over the shared transport (package T, transport.h): it
// owns one `remote::Server`, reads text lines from its `Connection`, executes
// each command synchronously against the backend (`jnext::dbg::Debugger`) and
// writes the reply and the prompt. It never touches a socket and never touches
// `Emulator`: the transport is T's, the machine is the backend's.
//
// Design: doc/design/debug-subsystem/zrcp-frontend.md. The wire is §1, the
// command table §2, the session and the run state machine §4, the transport
// model §5, the deliberate divergences from ZEsarUX 12.0 §10.
//
// ── WIRING ─────────────────────────────────────────────────────────────────
//
// Exactly D's shape (dzrp_server.h):
//
//     ZrcpServer zrcp(debugger);
//     zrcp.server().open(cfg.debug_listen_address, port);
//     debugger.add_service(zrcp.server());
//
// `platform/debug_servers.*` does that for every loop owner. The unit suite
// opens the same Server over T's `FakeListener`.
//
// ── THE SESSION ────────────────────────────────────────────────────────────
//
// One client per listener (a second gets `Error. Another ZRCP client is
// connected` and is closed — T's busy reply). The client is ATTACHED on
// connect, since every ZRCP command may touch the machine and every mutation is
// attributed; it is DETACHED on disconnect or `quit` (SES-01), which removes
// its subscriptions and releases a pause that is its own. cpu-step mode, the
// prompt, `set-cr`, the debug-settings byte and the partial T-state base are
// adapter state (§4.1); the backend learns none of them.
//
// ── THE RUN STATE MACHINE (§4.3) ───────────────────────────────────────────
//
// `run`, `run n` and `cpu-step-over` do not answer at once. While one is in
// flight (`RunKind` != None) no line is executed: ANY received byte stops the
// machine and the line it belongs to is discarded (§1.2). The stop reply is
// written the first time the adapter is asked after the machine stops —
// `on_service` at the top of a pump, or `on_notify` after its drain — so it
// is LEVEL-triggered on `state().paused`, not on the `Paused` edge. A
// `Reset{Hard}` while in flight completes the reply from the event (§4.6
// rule 4: never a pause). `run n` is a loop of `step_into`, parked between
// time slices so a large `n` never holds one pump (§2.2).
// ---------------------------------------------------------------------------

#include <array>
#include <chrono>
#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "debug/debugger.h"
#include "remote/transport.h"
#include "remote/zrcp/zrcp_condition.h"

namespace jnext {
namespace remote {
namespace zrcp {

/// The welcome ZEsarUX 12.0 sends on connect, byte for byte ([T1]) — the
/// prompt included, so a client sees a complete reply.
constexpr char WELCOME[] =
    "Welcome to ZEsarUX remote command protocol (ZRCP)\n"
    "Write help for available commands\n"
    "\n"
    "command> ";

/// The two prompts (§1.1): outside and inside cpu-step mode. DeZog reads the
/// server's state as everything after `command`.
constexpr char PROMPT[]      = "command> ";
constexpr char PROMPT_STEP[] = "command@cpu-step> ";

/// What a second concurrent client is told before it is closed (§5.5).
constexpr char BUSY_REPLY[] = "Error. Another ZRCP client is connected\n";

/// `run`'s first line (§1.2) — the only part of its reply sent at once.
constexpr char RUNNING_UNTIL[] =
    "Running until a breakpoint, key press or data sent, menu opening or other event";

/// The largest `run n` served (§2.2).
constexpr std::uint32_t RUN_LIMIT_MAX = 1000000;

/// ZEsarUX's `MAX_BREAKPOINTS_CONDITIONS`: slots 1..100 (§1.5).
constexpr int BREAKPOINT_SLOTS = 100;

/// ZEsarUX's `MAX_BREAKPOINT_CONDITION_LENGTH`: the longest condition or
/// breakpoint action accepted.
constexpr std::size_t BREAKPOINT_TEXT_MAX = 256;

/// How a command is answered (§2).
enum class CommandClass : std::uint8_t {
    Served,       ///< mapped onto the backend
    Declined,     ///< a deliberate refusal, listed in `help` with its reason
    Unsupported,  ///< a ZEsarUX command jnext does not serve: an honest error
};

/// One row of the command table, as the suite's census reads it.
struct CommandInfo {
    const char*  name;
    const char*  aliases;  ///< ZEsarUX's spelling, `|gr` / `|exit|logout`; nullptr = none
    CommandClass cls;
};

class ZrcpServer final : public Protocol, public jnext::dbg::Listener {
public:
    /// The clock `run n` slices its work by. Empty = `steady_clock::now`; the
    /// unit suite passes its own, so a parked `run n` is tested without timing.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit ZrcpServer(jnext::dbg::Debugger& dbg, Clock clock = {});

    /// Unregisters its Server from the backend and ends a live session
    /// (`on_disconnect()` runs, so the client is detached). The `Debugger` must
    /// outlive this object.
    ~ZrcpServer() override;

    ZrcpServer(const ZrcpServer&)            = delete;
    ZrcpServer& operator=(const ZrcpServer&) = delete;

    /// The transport this adapter serves on: `open()` it, `add_service()` it.
    Server& server() { return server_; }

    /// The command table, every row: served, declined and unsupported.
    static std::vector<CommandInfo> command_table();

    // ── remote::Protocol ──────────────────────────────────────────────────
    void                    on_connect(Connection& c) override;
    jnext::dbg::ServiceStep on_service(Connection& c) override;
    void                    on_notify(Connection& c) override;
    void                    on_disconnect() override;

    // ── dbg::Listener (SES-02) ────────────────────────────────────────────
    void on_paused(const jnext::dbg::PausedInfo& info) override;
    void on_resumed(jnext::dbg::ClientId by) override;
    void on_reset(jnext::dbg::ResetKind kind) override;
    void on_frame_ended(std::uint32_t frame) override;
    void on_subscriptions_changed(jnext::dbg::EventKindMask kinds) override;
    void on_exit_requested(int code) override;
    void on_log(jnext::dbg::LogLevel level, const std::string& text) override;

private:
    /// One parsed command line.
    struct Cmd {
        std::string              name;
        std::string              params;  ///< everything after the first space
        std::vector<std::string> args;    ///< `params` split on whitespace
    };

    /// One row of THE table. `help`, `ls`, the dispatcher and the census all
    /// read it, so a command cannot be listed and not served, or the reverse.
    struct CommandDef {
        const char*  name;
        const char*  aliases;
        const char*  params;
        const char*  help;
        CommandClass cls;
        void (ZrcpServer::*run)(const Cmd&);
    };
    static const CommandDef COMMANDS[];
    static const CommandDef* find_command(const std::string& name);

    /// What is in flight (§4.1 `in_run`).
    enum class RunKind : std::uint8_t { None, Run, RunLimit, StepOver };

    // Output. `send` writes raw text (set-cr applied); `reply` is a complete
    // answer: `body`, a newline, the prompt.
    const char* prompt() const { return step_mode_ ? PROMPT_STEP : PROMPT; }
    void send(const std::string& text);
    void reply(const std::string& body);

    // Input.
    void pull_input(Connection& c);
    bool take_line(std::string& line);
    void execute_line(const std::string& line);

    // The run state machine.
    jnext::dbg::ServiceStep service_run();
    void run_slice();
    void finish_run(bool limit_reached);
    std::string stop_reply(const std::string& fired) const;
    std::string fired_text(const jnext::dbg::RunState& st) const;
    std::string tstates_text() const;
    void drop_transients();
    void end_session();

    // Common error text for a refused execute-family verb.
    static std::string refusal_text(jnext::dbg::Result r, const char* what);

    // Handlers — WP-1 session and information.
    void cmd_about(const Cmd& c);
    void cmd_get_version(const Cmd& c);
    void cmd_get_buildnumber(const Cmd& c);
    void cmd_get_cpu_core_name(const Cmd& c);
    void cmd_get_os(const Cmd& c);
    void cmd_empty(const Cmd& c);
    void cmd_help(const Cmd& c);
    void cmd_ls(const Cmd& c);
    void cmd_set_cr(const Cmd& c);
    void cmd_quit(const Cmd& c);
    void cmd_get_debug_settings(const Cmd& c);
    void cmd_set_debug_settings(const Cmd& c);
    // WP-2 — inspection.
    void cmd_get_registers(const Cmd& c);
    void cmd_set_register(const Cmd& c);
    void cmd_read_memory(const Cmd& c);
    void cmd_write_memory(const Cmd& c);
    void cmd_write_memory_raw(const Cmd& c);
    void cmd_hexdump(const Cmd& c);
    void cmd_get_crc32(const Cmd& c);
    void cmd_disassemble(const Cmd& c);
    void cmd_get_memory_pages(const Cmd& c);
    void cmd_get_stack_backtrace(const Cmd& c);
    void cmd_get_tstates(const Cmd& c);
    void cmd_get_tstates_partial(const Cmd& c);
    void cmd_reset_tstates_partial(const Cmd& c);
    void cmd_get_cpu_frequency(const Cmd& c);
    void cmd_get_current_machine(const Cmd& c);
    void cmd_tbblue_get_register(const Cmd& c);
    void cmd_tbblue_set_register(const Cmd& c);
    void cmd_tbblue_get_sprite(const Cmd& c);
    void cmd_tbblue_set_sprite(const Cmd& c);
    void cmd_tbblue_get_pattern(const Cmd& c);
    void cmd_tbblue_set_pattern(const Cmd& c);
    void cmd_tbblue_get_palette(const Cmd& c);
    void cmd_tbblue_set_palette(const Cmd& c);
    void cmd_tbblue_get_clipwindow(const Cmd& c);
    void cmd_tbblue_set_clipwindow(const Cmd& c);
    void cmd_write_port(const Cmd& c);
    // WP-3 — control.
    void cmd_enter_cpu_step(const Cmd& c);
    void cmd_exit_cpu_step(const Cmd& c);
    void cmd_cpu_step(const Cmd& c);
    void cmd_cpu_step_over(const Cmd& c);
    void cmd_run(const Cmd& c);
    void cmd_hard_reset_cpu(const Cmd& c);
    void cmd_reset_cpu(const Cmd& c);
    void cmd_generate_nmi(const Cmd& c);
    // WP-4 — breakpoints and conditions (§2.4, §3, §4.1-4.2).
    void cmd_clear_membreakpoints(const Cmd& c);
    void cmd_disable_breakpoint(const Cmd& c);
    void cmd_disable_breakpoints(const Cmd& c);
    void cmd_enable_breakpoint(const Cmd& c);
    void cmd_enable_breakpoints(const Cmd& c);
    void cmd_evaluate(const Cmd& c);
    void cmd_get_breakpoints(const Cmd& c);
    void cmd_get_breakpointsactions(const Cmd& c);
    void cmd_get_membreakpoints(const Cmd& c);
    void cmd_set_breakpoint(const Cmd& c);
    void cmd_set_breakpointaction(const Cmd& c);
    void cmd_set_membreakpoint(const Cmd& c);
    // WP-5 — history, stack, coverage, load (§2.2-2.3, §4.1).
    void cmd_cpu_code_coverage(const Cmd& c);
    void cmd_cpu_history(const Cmd& c);
    void cmd_extended_stack(const Cmd& c);
    void cmd_load_binary(const Cmd& c);
    void cmd_save_binary(const Cmd& c);
    void cmd_smartload(const Cmd& c);
    void cmd_snapshot_load(const Cmd& c);
    void cmd_snapshot_save(const Cmd& c);

    /// `cpu-history`'s view of the trace: oldest first, with the session's
    /// `ignrephalt` / `ignrepldxr` filters applied. Rebuilt only when the
    /// machine has moved or the trace or a filter changed (`hist_key_`).
    const std::vector<::TraceEntry>& history_view();
    std::array<std::uint16_t, 8> history_mmu(const ::TraceEntry& e) const;
    /// What a session switch owns of a machine-wide one: turned on by this
    /// session, so its `enabled no` / end turns it off; on already, it is left.
    struct Owned {
        bool on    = false;  ///< this session's view: enabled
        bool owned = false;  ///< this session switched the backend on
    };

    /// ZEsarUX's "On Change" (`debug_breakpoints_conditions_saltado`) for a
    /// PC-free slot: its condition's value at the last boundary it was
    /// evaluated at, and whether that evaluation was a false→true edge.
    struct Edge {
        bool          prev  = false;
        bool          fired = false;
        /// The boundary (master cycle) `prev` / `fired` describe. A second
        /// evaluation at the same boundary — a `probe_execute`, the landing
        /// check — returns `fired` and advances nothing.
        std::uint64_t cycle = UINT64_MAX;
        bool step(std::uint64_t at, bool v) {
            if (at == cycle) return fired;
            cycle = at;
            fired = v && !prev;
            prev  = v;
            return fired;
        }
    };

    /// One condition slot (§4.1 `slots[1..100]`).
    struct Slot {
        Translation            cond;          ///< `cond.canonical` is what is listed and echoed
        jnext::dbg::Condition  predicate;     ///< compiled from `cond`; empty = always true
        bool                   has_cond = false;
        bool                   enabled  = false;
        std::string            action;        ///< as set; empty / menu / break = Stop
        jnext::dbg::EventId    sub = jnext::dbg::EVENT_NONE;
        /// Set while armed, for a PC-free slot only (fast-path slots fire on
        /// every arrival — the PC term is false at the instruction before).
        std::shared_ptr<Edge>  edge;
    };
    /// One run of equal non-zero memory-breakpoint type (§4.2).
    struct MemRange {
        std::uint16_t       lo = 0, hi = 0;
        std::uint8_t        type = 0;
        jnext::dbg::EventId sub  = jnext::dbg::EVENT_NONE;
    };

    void arm_slot(int index);
    void sync_mem_ranges();
    bool slot_fires_at(int index, std::uint16_t pc) const;
    bool slot_edge_at(int index, std::uint16_t pc);
    bool other_breakpoint_at(std::uint16_t pc) const;
    std::string evaluate_text(const std::string& expr) const;
    void queue_action_log(int index);
    void flush_logs();

    // Helpers.
    std::vector<std::uint8_t> read_cpu(std::uint32_t addr, std::size_t n) const;
    void cpu_step_reply();

    jnext::dbg::Debugger& dbg_;
    Clock                 clock_;
    Server                server_;

    // Per-connection state.
    Connection*          conn_ = nullptr;  // valid inside a Protocol callback only
    jnext::dbg::ClientId cid_  = jnext::dbg::CLIENT_NONE;
    std::string          rx_;               // received, not yet executed
    bool                 discard_to_eol_ = false;  // the rest of an interrupting line

    // §4.1 — the session.
    bool          step_mode_      = false;
    bool          cr_mode_        = false;
    std::uint32_t debug_settings_ = 1;  // ZEsarUX's own initial value ([T1])
    std::uint64_t tstates_base_   = 0;

    // §4.3 — what is in flight.
    RunKind       in_run_        = RunKind::None;
    std::uint32_t run_limit_     = 0;
    std::uint32_t run_remaining_ = 0;
    /// §4.6 rule 4 — a Reset{Hard} answered the run in flight; its plain stop
    /// reply is owed at the next callback.
    bool          reset_stop_owed_ = false;
    /// `run n` stopped on landing where one of this session's slots fires:
    /// that slot, 0-based, for the `fired` line; -1 = none.
    int           run_landed_slot_ = -1;

    // §4.1 — breakpoints (WP-4).
    bool                                bp_master_ = false;
    std::array<Slot, BREAKPOINT_SLOTS>  slots_{};
    std::vector<std::uint8_t>           mem_types_ = std::vector<std::uint8_t>(65536, 0);
    std::vector<MemRange>               mem_ranges_;
    /// Lines a print action produced at a boundary, sent at the next callback.
    std::vector<std::string>            pending_logs_;

    // §4.1 — history, extended stack, coverage (WP-5).
    Owned          hist_;
    bool           hist_started_ = false;
    bool           ign_halt_     = false;
    bool           ign_ldxr_     = false;
    /// `set-max-size`: a VIEW limit — the newest `hist_max_` entries are shown.
    /// The machine's trace (which jnext's Step Back reads) is never resized.
    std::uint32_t  hist_max_     = 10000;   // ZEsarUX reports a size; the trace log's own default
    /// `clear`: a VIEW base — the newest machine-trace entry at the clear
    /// (`cycle`, `pc`); only entries after it are shown. The machine's trace
    /// is never cleared.
    bool           hist_has_base_ = false;
    std::uint64_t  hist_base_cycle_ = 0;
    std::uint16_t  hist_base_pc_    = 0;
    std::uint64_t  hist_gen_     = 0;       // bumped by clear / resize / enable / a filter
    std::array<std::uint64_t, 3> hist_key_{{~0ull, ~0ull, ~0ull}};
    std::vector<::TraceEntry> hist_view_;
    Owned          xstack_;
    Owned          cov_;
};

}  // namespace zrcp
}  // namespace remote
}  // namespace jnext
