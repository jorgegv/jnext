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
};

// Access to FUSE tstates counter (for contention delay injection)
extern "C" uint32_t* fuse_z80_tstates_ptr(void);

// Forward declaration — MachineType defined in contention.h
enum class MachineType;
class ContentionModel;
class Mmu;

/// Build the FUSE Z80 core's internal contention tables (ula_contention[],
/// memory_map_read[]) for the given machine type.  Must be called at init
/// and whenever the machine type changes.
///
/// **Phase-2 contention wiring (2026-04-26)**: the FUSE table is no longer
/// the source of contention delays — `Z80Cpu::set_contention_runtime()`
/// installs `ContentionModel::contention_tick()` as the per-cycle path.
/// This function is retained so FUSE link symbols (`memory_map_read[]`,
/// `ula_contention[]`) stay defined; the populated values are unused.
void z80_build_contention_tables(MachineType type);

/// Update contention flag for a specific 8KB memory page (0-7).
/// page 0 = 0x0000-0x1FFF, page 2 = 0x4000-0x5FFF, page 6 = 0xC000-0xDFFF, etc.
/// Called when 128K bank paging changes which RAM bank is at 0xC000.
///
/// **Phase-2 contention wiring (2026-04-26)**: this is now a no-op kept for
/// compile-time compatibility. Per-page contention is queried from
/// `ContentionModel::is_contended_access()` driven by `mem_active_page`.
void z80_set_page_contended(int page, bool contended);

/// Install the per-cycle contention runtime (Phase-2 wiring, 2026-04-26).
/// `cm` is the `ContentionModel` whose `contention_tick()` is called per
/// memory/IO bus cycle. `mmu` provides the `mem_active_page` decode for
/// the 8K page underlying each cycle's address. `machine_type` selects the
/// per-machine timing (`tstates_per_line`) used to derive `(hc, vc)` from
/// the FUSE `tstates` counter. Pass null pointers to disable (the FUSE
/// callbacks then add the legacy `+3` non-contended T-states only).
void z80_set_contention_runtime(ContentionModel* cm, Mmu* mmu, MachineType machine_type);

// Z80 CPU wrapper — backed by FUSE Z80 core (third_party/fuse-z80/)
class Z80Cpu {
public:
    Z80Cpu(MemoryInterface& mem, IoInterface& io);

    void reset();
    int  execute();   // execute one instruction; returns T-states used

    Z80Registers get_registers() const { return regs_; }
    void set_registers(const Z80Registers& r) { regs_ = r; }

    void request_interrupt(uint8_t vector);
    void request_nmi();
    bool is_halted() const { return regs_.halted; }

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

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    MemoryInterface& mem_;
    IoInterface&     io_;
    Z80Registers     regs_{};
    bool             nmi_pending_ = false;
    bool             int_pending_ = false;
    uint8_t          int_vector_  = 0xFF;
    uint32_t         int_requested_at_ = 0;  // FUSE tstates when /INT was asserted
};
