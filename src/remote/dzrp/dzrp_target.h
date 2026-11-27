#pragma once

// ---------------------------------------------------------------------------
// DzrpTarget: a `jnext::dbg::Target` for a REMOTE JNEXT reached over DZRP (GH #515,
// epic #486; R2.1, R2.2, R2.3 and R2.7 of doc/design/DEBUGGER-REMOTE-REAL-NEXT-PLAN.md).
//
// A `Debugger` built over one of these (`Debugger(Target&)`) is the same facade the
// local debugger is, over a machine in another process: registers, memory, the slots,
// NextREGs, ports, sprites, run and pause, execute breakpoints and watchpoints. The
// facade owns the semantics (conditions, handlers, `once`, the subscription table);
// this class owns the wire.
//
// ── WHAT THE OWNER DOES ─────────────────────────────────────────────────────
//   * builds a `DzrpClient` (the asynchronous connect is GH #517's), waits for `Ready`,
//     and hands it over: this class never connects;
//   * installs the client's wait hook (the LOCAL facade's `pump()`, so that this
//     process's own servers keep being served while a request waits, D17);
//   * registers the target as a service of the remote facade (`remote.add_service()`),
//     which makes `pump()` call `service_once()`;
//   * destroys the remote `Debugger` (and so removes the service) BEFORE the target.
//     Destroying the target ends the session: the client writes CMD_CLOSE.
//
// ── CHEAP READS ─────────────────────────────────────────────────────────────
// `info()`, `capabilities()`, `why_unavailable()`, `register_validity()`, `run_state()`
// and `take_stop()` answer from members: NO I/O, whatever the link is doing.
//
// ── THE CACHE (D3) ──────────────────────────────────────────────────────────
// A request is a round trip, so reads are served from a cache that is dropped when the
// answer can have changed: at every resume, at every `service_once()` while the target
// runs (so a read while running is fetched at most once per remote pump), and after any
// write that can move memory (a NextREG or port access). The first machine read after a
// stop sends, pipelined in one round trip, the registers and the memory around the stop
// (the PC, the SP): conditions read those. A miss fetches every missing line of the
// request in one burst. Every WRITE is pipelined with the re-read of what it can change,
// so the next read needs no fetch and shows what the remote really holds (ROM-dropped
// bytes, NextREG side effects) rather than what was sent.
//
// ── FAILURE ─────────────────────────────────────────────────────────────────
// A timeout, a lost link or a protocol error (a reply of the wrong length, a duplicate
// breakpoint id) ends the session: the target is LOST. Capabilities empty with the reason,
// every verb `Unsupported` (never `RefusedUnavailable`: the capability is gone, and the
// facade logs a MUTATE for a short `poke(Page)` under that status), a direct-value query
// its zero value. `connected()` and `lost_reason()` are for GH #517's lost-link banner.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "debug/debugger.h"
#include "debug/target.h"
#include "remote/dzrp/dzrp_client.h"

namespace jnext {
namespace remote {
namespace dzrp {

class DzrpTarget final : public jnext::dbg::Target, public jnext::dbg::Service {
public:
    /// `client` must be `Ready`; anything else makes the target lost at once.
    explicit DzrpTarget(std::unique_ptr<DzrpClient> client);
    ~DzrpTarget() override;
    DzrpTarget(const DzrpTarget&)            = delete;
    DzrpTarget& operator=(const DzrpTarget&) = delete;

    bool               connected() const;
    const std::string& lost_reason() const { return lost_reason_; }
    DzrpClient&        client() { return *cl_; }

    // ---- jnext::dbg::Target ------------------------------------------------
    jnext::dbg::TargetInfo       info() const override { return info_; }
    jnext::dbg::CapabilitySet    capabilities() const override { return caps_; }
    std::string                  why_unavailable(jnext::dbg::Capability c) const override;
    jnext::dbg::RegisterValidity register_validity() const override { return validity_; }

    jnext::dbg::RunState run_state() const override;
    jnext::dbg::Result   step_instruction() override { return jnext::dbg::Result::Unsupported; }

    Z80Registers registers() const override;
    jnext::dbg::Result       set_register(jnext::dbg::RegId reg, uint16_t value) override;

    jnext::dbg::Expected<size_t> read(jnext::dbg::MemSpace space, uint32_t addr, size_t n,
                                      uint8_t* buf) const override;
    jnext::dbg::Expected<size_t> write(jnext::dbg::MemSpace space, uint32_t addr, size_t n,
                                       const uint8_t* buf) override;

    std::array<jnext::dbg::SlotInfo, 8> mmu_slots() const override;
    jnext::dbg::Result                  set_mmu_slot(int slot, uint8_t page) override;
    jnext::dbg::MemSpace                rom_select() const override { return jnext::dbg::MemSpace::cpu(); }

    uint8_t                         nextreg_read(uint8_t reg) const override;
    jnext::dbg::Result              nextreg_write(uint8_t reg, uint8_t value) override;
    jnext::dbg::Expected<uint8_t>   port_in(uint16_t port) override;
    jnext::dbg::Result              port_out(uint16_t port, uint8_t value) override;

    std::vector<jnext::dbg::SpriteInfo> sprites() const override;
    jnext::dbg::Expected<std::array<uint8_t, jnext::dbg::SPRITE_ATTR_BYTES>>
                                  sprite_attr_raw(uint8_t idx) const override;
    jnext::dbg::Result            set_sprite_attr_raw(uint8_t, const uint8_t*, size_t) override {
        return jnext::dbg::Result::Unsupported;
    }
    jnext::dbg::ConstBytes        pattern_ram() const override;
    jnext::dbg::Result            write_pattern_ram(uint16_t, const uint8_t*, size_t) override {
        return jnext::dbg::Result::Unsupported;
    }
    jnext::dbg::Expected<uint16_t> sprite_palette_rgb333(int bank, uint8_t index) const override;
    jnext::dbg::ClipWindow         sprite_clip() const override;

    jnext::dbg::Result resume() override;
    jnext::dbg::Result pause() override;
    bool               take_stop(jnext::dbg::TargetStop& out) override;
    jnext::dbg::Expected<uint32_t> add_breakpoint(uint16_t addr, uint16_t page) override;
    jnext::dbg::Result             remove_breakpoint(uint32_t handle) override;
    jnext::dbg::Expected<uint32_t> add_watchpoint(uint16_t lo, uint16_t hi, uint16_t page,
                                                  jnext::dbg::Access access) override;
    jnext::dbg::Result             remove_watchpoint(uint32_t handle) override;

    // ---- jnext::dbg::Service -----------------------------------------------
    jnext::dbg::ServiceStep service_once(int wait_ms) override;
    void                    flush_notifications() override {}
    bool                    peer_connected() const override { return connected(); }

private:
    using Bytes = std::vector<std::uint8_t>;
    using Line  = std::array<std::uint8_t, 256>;

    bool usable() const;
    /// A reply that is not `Ok` ends the session when the client says so; `Reentrant` and
    /// the gate's refusals leave it standing. Returns true for `Ok`.
    bool good(const Reply& r) const;
    void lose(const std::string& why) const;
    /// A reply of a fixed length is a protocol error when it is any other.
    bool length_is(const Reply& r, std::size_t n) const;
    void compute_capabilities() const;
    void drop_cache() const;
    void drop_cpu_lines() const;

    /// The first machine read after a stop or the connect: registers + the memory around it.
    void prime() const;
    bool ensure_registers() const;
    bool fetch(bool want_regs, const std::vector<int>& cpu_lines) const;
    bool store_registers(const Bytes& p) const;
    bool fetch_page_lines(uint8_t page, const std::vector<int>& lines) const;
    bool fetch_nr_line(unsigned line) const;
    bool fetch_sprites() const;
    bool fetch_patterns() const;
    bool fetch_palette(int bank) const;
    bool fetch_clip() const;
    /// Requests that read `lines` of the CPU view, one burst (CMD_READ_MEM_BLOCKS, or a
    /// CMD_READ_MEM per run when the remote has no 28).
    std::vector<std::pair<std::uint8_t, Bytes>> mem_requests(const std::vector<int>& lines) const;
    bool store_mem_replies(const std::vector<int>& lines, const std::vector<Reply>& replies,
                           std::size_t first) const;
    std::vector<int> missing_cpu_lines(uint32_t addr, size_t n) const;
    jnext::dbg::Result submit_ack(std::uint8_t id, Bytes payload);
    void on_pause_notification(const PauseNotification& n);

    std::unique_ptr<DzrpClient> cl_;

    // identity: members, set at construction and on loss
    jnext::dbg::TargetInfo                info_;
    mutable jnext::dbg::CapabilitySet     caps_;
    mutable jnext::dbg::RegisterValidity  validity_;
    mutable std::array<std::string, jnext::dbg::CAPABILITY_COUNT> why_;
    mutable bool        lost_ = false;
    mutable std::string lost_reason_;

    // run control
    int                                    owed_ = 0;   ///< NTF_PAUSE the remote owes this client
    std::optional<jnext::dbg::TargetStop>  stop_;
    mutable bool                           burst_pending_ = true;
    bool                                   have_hint_ = false;   ///< the last stop named the PC
    mutable bool                           sp_known_ = false;
    mutable uint16_t                       pc_hint_ = 0;
    mutable uint16_t                       sp_hint_ = 0;

    // breakpoints and watchpoints
    std::map<uint16_t, std::pair<uint16_t, std::uint8_t>> bps_;   ///< id -> (addr, bank+1)
    struct Watch { uint16_t addr; std::uint8_t bank1; uint16_t size; std::uint8_t access; };
    std::map<uint32_t, Watch> wps_;
    uint32_t                  next_wp_ = 1;

    // the cache
    mutable bool                          regs_valid_ = false;
    mutable Z80Registers      regs_{};
    mutable std::array<std::uint8_t, 8>   slot_bytes_{};
    mutable std::map<uint32_t, Line>      lines_;   ///< cpu: line; page: 0x10000 + page*32 + line
    mutable std::array<bool, 16>          nr_valid_{};
    mutable std::array<std::uint8_t, 256> nr_{};
    mutable bool                          spr_valid_ = false;
    mutable Bytes                         spr_;
    mutable bool                          pat_valid_ = false;
    mutable Bytes                         pat_;
    mutable std::array<bool, 2>           pal_valid_{};
    mutable std::array<std::array<uint16_t, 256>, 2> pal_{};
    mutable bool                          clip_valid_ = false;
    mutable jnext::dbg::ClipWindow        clip_{};
};

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
