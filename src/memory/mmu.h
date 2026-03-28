#pragma once
#include <cstdint>
#include "ram.h"
#include "rom.h"
#include "cpu/z80_cpu.h"
#include "debug/debug_state.h"

class DivMmc;  // forward declaration for overlay

class Mmu : public MemoryInterface {
public:
    Mmu(Ram& ram, Rom& rom);

    // Reset MMU to power-on state (from VHDL zxnext.vhd)
    void reset();

    // Set slot to page number; rebuilds fast-dispatch pointer
    void set_page(int slot, uint8_t page);
    uint8_t get_page(int slot) const { return slots_[slot]; }

    // DivMMC memory overlay — set by Emulator when DivMMC is initialized.
    // Kept as raw pointer for zero-overhead hot path.
    void set_divmmc(DivMmc* d) { divmmc_ = d; }

    // Debugger state — set by Emulator for data breakpoint checking.
    void set_debug_state(DebugState* ds) { debug_state_ = ds; }

    // Hot-path memory access (inline for performance)
    inline uint8_t read(uint16_t addr) override {
        // DivMMC overlay: intercept reads from 0x0000-0x3FFF when active
        if (divmmc_ && addr < 0x4000) {
            uint8_t val;
            if (divmmc_read(addr, val)) {
                // Check data breakpoints (only when debugger is active with watchpoints)
                if (debug_state_ && debug_state_->active() &&
                    debug_state_->breakpoints().has_any_watchpoints() &&
                    debug_state_->breakpoints().has_watchpoint(addr, WatchType::READ)) {
                    debug_state_->set_data_bp_hit(true);
                    debug_state_->set_data_bp_addr(addr);
                }
                return val;
            }
        }
        int slot = addr >> 13;
        const uint8_t* ptr = read_ptr_[slot];
        if (!ptr) return 0xFF;
        uint8_t val = ptr[addr & 0x1FFF];
        // Check data breakpoints (only when debugger is active with watchpoints)
        if (debug_state_ && debug_state_->active() &&
            debug_state_->breakpoints().has_any_watchpoints() &&
            debug_state_->breakpoints().has_watchpoint(addr, WatchType::READ)) {
            debug_state_->set_data_bp_hit(true);
            debug_state_->set_data_bp_addr(addr);
        }
        return val;
    }

    inline void write(uint16_t addr, uint8_t val) override {
        // Check data breakpoints (only when debugger is active with watchpoints)
        if (debug_state_ && debug_state_->active() &&
            debug_state_->breakpoints().has_any_watchpoints() &&
            debug_state_->breakpoints().has_watchpoint(addr, WatchType::WRITE)) {
            debug_state_->set_data_bp_hit(true);
            debug_state_->set_data_bp_addr(addr);
        }
        // DivMMC overlay: intercept writes to 0x0000-0x3FFF when active
        if (divmmc_ && addr < 0x4000) {
            if (divmmc_write(addr, val)) return;
        }
        // Layer 2 write-over: redirect writes to L2 RAM banks.
        if (l2_write_enable_ && addr < 0xC000) {
            int segment = addr / 0x4000;  // 0, 1, or 2
            if (l2_segment_mask_ & (1 << segment)) {
                // Write to L2 RAM: each segment is 16K = 2 pages.
                // L2 bank N → pages N*2, N*2+1; three consecutive banks.
                uint16_t l2_page = static_cast<uint16_t>((l2_bank_ + segment) * 2);
                uint16_t offset = addr % 0x4000;
                uint8_t* p = ram_.page_ptr(l2_page + (offset >> 13));
                if (p) p[offset & 0x1FFF] = val;
                return;
            }
        }
        int slot = addr >> 13;
        if (read_only_[slot]) return;
        uint8_t* ptr = write_ptr_[slot];
        if (!ptr) return;
        ptr[addr & 0x1FFF] = val;
    }

    // Apply 128K banking: port 0x7FFD value maps slots 0/1/6/7
    void map_128k_bank(uint8_t port_7ffd);

    // Map ROM page into slot (read-only)
    void map_rom(int slot, uint8_t rom_page);

    // ---------------------------------------------------------------
    // Layer 2 write-over control (driven by port 0x123B)
    // ---------------------------------------------------------------

    /// Configure Layer 2 write-mapping from port 0x123B value.
    ///   bit 0: write-map enable
    ///   bits 7:6: segment select (00=0x0000, 01=0x4000, 10=0x8000, 11=all)
    void set_l2_write_port(uint8_t val, uint8_t active_bank);

private:
    void rebuild_ptr(int slot);

    Ram& ram_;
    Rom& rom_;
    uint8_t slots_[8];
    const uint8_t* read_ptr_[8];
    uint8_t*       write_ptr_[8];
    bool           read_only_[8];
    bool           paging_locked_ = false;

    // Layer 2 write-over state
    bool    l2_write_enable_  = false;
    uint8_t l2_segment_mask_  = 0;     // bitmask: bit 0=seg0, bit 1=seg1, bit 2=seg2
    uint8_t l2_bank_          = 8;     // 16K bank base (from NextREG 0x12)

    // DivMMC overlay (non-owning pointer, set by Emulator)
    DivMmc* divmmc_ = nullptr;

    // Debugger state (non-owning pointer, set by Emulator)
    DebugState* debug_state_ = nullptr;

    // Out-of-line DivMMC helpers (defined in mmu.cpp to avoid circular include)
    bool divmmc_read(uint16_t addr, uint8_t& val) const;
    bool divmmc_write(uint16_t addr, uint8_t val);
};
