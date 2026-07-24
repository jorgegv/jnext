#pragma once
#include <cstdint>
#include <functional>

struct Z80Registers {
    uint16_t AF, BC, DE, HL;
    uint16_t AF2, BC2, DE2, HL2;
    uint16_t IX, IY, SP, PC;
    uint16_t MEMPTR;           // hidden WZ register
    uint8_t  I, R;
    uint8_t  IFF1, IFF2, IM;
    uint8_t  Q;                // internal F-assembly register
    bool halted;
    // Pass-9 fix: VHDL t80n.vhd:1358-1367 — IncDecZ is a 1-bit shadow
    // latch updated from BC dec/inc in block-transfer instructions
    // (LDI/LDD/LDIR/LDDR/CPI/CPD/CPIR/CPDR + Z80N LDIX/LDIRX/LDDX/LDDRX/
    // LDPIRX/LDIRSCALE) and from F_Out(Flag_Z) of DJNZ. The I_BT block
    // (t80n.vhd:1283-1284) overrides F.P with IncDecZ for I_BT/I_BC
    // instructions — observable in LDWS (Z80N ED A5), where the spec
    // says "preserve P" but the VHDL emits IncDecZ from the most recent
    // 16-bit inc/dec. Stored as 0 or 1; reset = 0.
    uint8_t  IncDecZ;
};

class MemoryInterface {
public:
    virtual ~MemoryInterface() = default;
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t val) = 0;
};

class IoInterface {
public:
    virtual ~IoInterface() = default;
    virtual uint8_t in(uint16_t port) = 0;
    virtual void out(uint16_t port, uint8_t val) = 0;

    /// Z80N `NEXTREG rr,nn` / `NEXTREG rr,A` write (GH #54).
    ///
    /// VHDL zxnext.vhd:4739-4744 — the opcode raises `cpu_requester_0` and
    /// supplies the target register from `Z80N_data_s(15 downto 8)` (the
    /// opcode's own operand). The port path is a DIFFERENT requester
    /// (`cpu_requester_1 = port_253b_wr`, register = `nr_register`), and
    /// `nr_register` — the port-0x243B select latch — is written ONLY by an
    /// actual write to port 0x243B (zxnext.vhd:4592-4603). So the opcode
    /// must NOT disturb the select latch: a subsequent `IN A,(0x253B)` still
    /// reads whatever register port 0x243B last selected.
    ///
    /// The default implementation is the (VHDL-incorrect) port pair, kept
    /// only for the bare IoInterface stubs used by unit tests that have no
    /// NextReg file at all. PortDispatch overrides it.
    virtual void nextreg_opcode_write(uint8_t reg, uint8_t val) {
        out(0x243B, reg);
        out(0x253B, val);
    }
};

// Access to FUSE tstates counter (for contention delay injection)
extern "C" uint32_t* fuse_z80_tstates_ptr(void);

// Forward declaration — MachineType defined in contention.h
enum class MachineType;
class ContentionModel;
class Mmu;

// G53 (2026-05-01) — z80_build_contention_tables() and
// z80_set_page_contended() were retired with the FUSE legacy
// contention-table consumer. The FUSE in-opcode contention path now
// goes through ContentionModel::contention_tick() via CORETEST function
// overrides in z80_cpu.cpp (G141). All callers were removed in the same
// commit.

/// Install the per-cycle contention runtime (Phase-2 wiring, 2026-04-26).
/// `cm` is the `ContentionModel` whose `contention_tick()` is called per
/// memory/IO bus cycle. `mmu` provides the `mem_active_page` decode for
/// the 8K page underlying each cycle's address. `machine_type` selects the
/// per-machine timing (`tstates_per_line`) used to derive `(hc, vc)` from
/// the FUSE `tstates` counter. Pass null pointers to disable (the FUSE
/// callbacks then add the legacy `+3` non-contended T-states only).
void z80_set_contention_runtime(ContentionModel* cm, Mmu* mmu, MachineType machine_type);

/// Origins of the ULA's own (display-relative) raster counters, in the same
/// units `derive_hc_vc()` produces: `hc_origin` = VHDL `ula_min_hactive`
/// (= `c_min_hactive - 12`, zxula_timing.vhd:423, i.e.
/// `VideoTiming::ula_prefetch_origin_hc()`), `vc_origin` = VHDL
/// `c_min_vactive` (= `VideoTiming::display_origin().y`).
///
/// `ContentionModel::contention_tick()` transcribes the VHDL contention gate
/// verbatim, and that gate is written against the ULA counters `i_hc`/`i_vc`
/// — which zxula_timing.vhd:441-452 resets to 0 at the start of the ACTIVE
/// DISPLAY, not at the start of the frame. The CPU-side callbacks derive raw
/// frame-relative `(hc, vc)` from the FUSE T-state counter, so they must be
/// rebased onto the ULA counters before the gate sees them. Task 50.
void z80_set_ula_counter_origins(int hc_origin, int vc_origin);

/// Task 51 — update the frame geometry (`s_tstates_per_line` /
/// `s_tstates_per_frame`) that `derive_hc_vc()` uses to map the FUSE
/// T-state counter onto raster coordinates, WITHOUT re-binding the
/// contention runtime pointers. Called from the frame-edge machine-timing
/// commit seam (Emulator::begin_new_frame()) when a runtime NR 0x03
/// tim_sel change takes effect — the VHDL switches every c_* constant on
/// `eff_nr_03_machine_timing` (zxnext.vhd:6694-6703), so the CPU-side
/// line length must follow at the same seam. `z80_set_contention_runtime()`
/// still sets the same fields at init from the MachineType default.
void z80_set_frame_geometry(int tstates_per_line, int tstates_per_frame);

/// Test-observability getter for the line geometry `derive_hc_vc()` is
/// currently using (T-states per line). Lets integration rows prove the
/// Task 51 frame-edge re-push actually reached the CPU side, which has
/// no other public observable.
int z80_get_tstates_per_line();

// Z80 CPU wrapper — backed by FUSE Z80 core (third_party/fuse-z80/)
class Z80Cpu {
public:
    Z80Cpu(MemoryInterface& mem, IoInterface& io);

    // hard=false = t80n soft-reset semantics (t80n.vhd:429-447 + empty
    // register-file reset branch at :1493-1498): PC/AF/AF'/I/R/IFF/IM reset,
    // SP=0xFFFF, BC/DE/HL/BC'/DE'/HL'/IX/IY PRESERVED. hard=true additionally
    // zeroes the register file (power-on-undefined model).
    void reset(bool hard = true);
    int  execute();   // execute one instruction; returns T-states used

    Z80Registers get_registers() const { return regs_; }
    void set_registers(const Z80Registers& r) { regs_ = r; }

    // Cheap read-only accessors (Task 27 C10): avoid a full Z80Registers
    // by-value copy on the per-instruction hot path when only the PC — or
    // a read-only view — is needed. get_registers() copies the whole
    // struct; these return the member directly.
    uint16_t pc() const { return regs_.PC; }
    const Z80Registers& registers() const { return regs_; }

    void request_interrupt(uint8_t vector);
    void request_nmi();
    bool is_halted() const { return regs_.halted; }
    bool stackless_retn_active() const { return stackless_retn_active_; }
    void set_stackless_retn_active_for_load(bool active) {
        stackless_retn_active_ = active;
    }

    // Pulse-mode INT-window machine-timing gate, per VHDL zxnext.vhd:2033:
    //   pulse_count_end <= pulse_count(5) and (machine_timing_48 or
    //                       machine_timing_p3 or pulse_count(2));
    // Translated: 48K and +3 release /INT after 32 CPU cycles (bit 5 alone);
    // 128K, Pentagon and Next-default require bit 5 AND bit 2 → 36 cycles.
    // Z80Cpu uses this to decide when a pending /INT pulse has elapsed and
    // must be discarded if the CPU never acknowledged it (e.g. interrupts
    // disabled inside an ISR). Defaults to true (32-cycle window) to
    // preserve the legacy behaviour for FUSE Z80 test runs that drive
    // Z80Cpu directly without an Emulator wiring this in.
    void set_machine_timing_48_or_p3(bool v) { machine_48_or_p3_ = v; }
    bool machine_timing_48_or_p3() const { return machine_48_or_p3_; }

    // Public memory access for Z80N instruction implementations
    MemoryInterface& memory() { return mem_; }
    IoInterface& io() { return io_; }

    // Contention callback: called on every memory read/write during instruction
    // execution. The callback should add delay to the tstates counter if the
    // address is in contended memory at the current video position.
    std::function<void(uint16_t addr)> on_contention;

    // Callback fired BEFORE opcode fetch — used for DivMMC automap
    // (must activate memory overlay before the opcode read).
    std::function<void(uint16_t pc)> on_m1_prefetch;

    // Callback fired AFTER opcode fetch — used for RETI detection and IM2
    std::function<void(uint16_t pc, uint8_t opcode)> on_m1_cycle;

    // Callback fired at the Z80 interrupt-acknowledge M1 cycle. Models the
    // VHDL IM2 daisy-chain vector delivery (see im2_device.vhd:155 +
    // peripherals.vhd:134-144 + zxnext.vhd:1999). When set AND an interrupt
    // is being serviced, the returned byte is used as the IM2 vector byte
    // delivered to the CPU; when unset, behaviour falls back to the legacy
    // `request_interrupt(vector)` path (int_vector_ member).
    //
    // This is opt-in: when nullptr, execute() is byte-identical to the
    // pre-callback behaviour — critical for preserving FUSE Z80 test
    // compliance (1356/1356), since the FUSE suite does not install it.
    std::function<uint8_t()> on_int_ack;

    // Magic breakpoint callback: fired when ED FF or DD 01 is executed.
    // If set and returns true, CPU should break to debugger.
    std::function<bool(uint16_t pc)> on_magic_breakpoint;

    // esxdos shim hook. Fired when PC reaches $0008 (RST $08 destination).
    // Z80Cpu reads the DEFB byte at the caller's return address (the
    // esxdos function code, e.g. $9A = F_OPEN) and passes it to the
    // callback along with a mutable copy of the current registers. If the
    // callback returns true, the call is intercepted: PC is set to
    // (return_addr + 1) to skip the DEFB, SP is bumped past the pushed
    // return addr (the RST $08 push is rolled back), and the callback's
    // updated registers are committed. If false, $0008 executes normally.
    //
    // Use case: jnext loads NEX files without booting NextZXOS, so the
    // real esxdos firmware (DivMMC AUTOMAP RST $08 handler) is not in
    // memory. z88dk-built NEX games that call esxdos_f_open etc. would
    // otherwise jump into 48K BASIC's ERROR handler and stall. With this
    // shim enabled (via --esxdos-stub), every esxdos call returns a
    // benign error so games' "no esxdos" fallback path is taken.
    std::function<bool(uint8_t defb_code, Z80Registers& regs)> on_esxdos_call;

    // Callback fired when an NMI is being serviced by the CPU. Receives the
    // PC value that will be pushed to stack as the NMI return address (i.e.
    // the post-HALT-fix PC, before fuse_z80_nmi() rewrites it to 0x0066).
    // Models the VHDL NR 0xC2/0xC3 capture window at zxnext.vhd:2050-2085 +
    // 6232-6236 (Z80N_command_s = NMIACK_LSB/MSB & cpu_wr_n='0'). Set by
    // Emulator::init() to latch NR 0xC2/0xC3 shadow registers (G88).
    std::function<void(uint16_t saved_pc)> on_nmi_servicing;

    // ZX Spectrum Next stackless-NMI bus substitution (NR 0xC0 bit 3).
    // The enable callback is sampled at NMI entry and before each following
    // instruction; clearing the bit abandons the shadow-return latch just as
    // zxnext.vhd clears z80_stackless_retn_en. The address callback supplies
    // the live C3:C2 value so an NMI handler may deliberately rewrite it.
    std::function<bool()> stackless_nmi_enabled;
    std::function<uint16_t()> stackless_nmi_return_address;

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// V20R-CPU-NIT-02 — Test observable: count of `request_interrupt()`
    /// invocations since the last `reset_request_interrupt_count()`. Used
    /// by the V20R-CPU-NIT-02-NO-DOUBLE-STAMP regression to assert that a
    /// single ULA/LINE pulse produces exactly ONE CPU /INT stamp post-fix
    /// (pre-fix the legacy scheduler callback and the V20 falling-edge
    /// poll BOTH stamped the same pulse → 2 calls).
    uint32_t request_interrupt_count() const { return request_interrupt_count_; }
    void reset_request_interrupt_count() { request_interrupt_count_ = 0; }

private:
    MemoryInterface& mem_;
    IoInterface&     io_;
    Z80Registers     regs_{};
    bool             nmi_pending_ = false;
    bool             int_pending_ = false;
    uint8_t          int_vector_  = 0xFF;
    uint32_t         int_requested_at_ = 0;  // FUSE tstates when /INT was asserted
    /// V20R-CPU-NIT-02 — Test-observable monotonic counter of
    /// `request_interrupt()` invocations. Reset via the public
    /// `reset_request_interrupt_count()`. Not persisted in save/load.
    uint32_t         request_interrupt_count_ = 0;
    // Default true → 32-cycle /INT pulse window. Matches VHDL machine_timing_48 /
    // machine_timing_p3 branch (zxnext.vhd:2033). Emulator overrides this to
    // false for 128K / Pentagon / Next-default (36-cycle window). Default-true
    // preserves byte-identical FUSE Z80 test behaviour (1356/1356) for tests
    // that instantiate Z80Cpu without an Emulator.
    bool             machine_48_or_p3_ = true;
    // Set after a stackless NMI acknowledge and consumed by RETN/RETI.
    // During the handler SP remains two below its entry value, exactly as
    // t80n's suppressed NMIACK writes dictate.
    bool             stackless_retn_active_ = false;
};
