#pragma once
#include <cstdint>
#include <array>
#include <functional>

// ZX Spectrum Next NextREG register file.
// Accessed via ports 0x243B (select) and 0x253B (data).
class NextReg {
public:
    NextReg();
    void reset();

    void    select(uint8_t reg);          // write to port 0x243B
    uint8_t read_selected();              // read from port 0x253B
    void    write_selected(uint8_t val);  // write to port 0x253B

    uint8_t read(uint8_t reg);
    void    write(uint8_t reg, uint8_t val);

    // Install a handler called when a specific register is written
    void set_write_handler(uint8_t reg, std::function<void(uint8_t)> fn);

    // Install a handler called when a specific register is read.
    // The handler returns the dynamic value; if no handler is set, the cached
    // value from regs_[] is returned.
    void set_read_handler(uint8_t reg, std::function<uint8_t()> fn);

    // Direct access to cached register value (bypasses read handler)
    uint8_t cached(uint8_t reg) const { return regs_[reg]; }

    // VHDL nr_03_config_mode state (zxnext.vhd:1102 default '1' at power-on,
    // transitioned only by NR 0x03 writes via bits[2:0] — see zxnext.vhd:5147-5151):
    //   bits[2:0] = 111   → config_mode = 1 (re-enter)
    //   bits[2:0] = 001-100 → config_mode = 0 (exit; also commits machine_type)
    //   bits[2:0] = 000   → no change
    // Owned by NextReg so it survives selected register shuffling. The rule
    // engine itself lives in Emulator's NR 0x03 write_handler, which calls
    // apply_nr_03_config_mode_transition() with the written low 3 bits.
    bool nr_03_config_mode() const { return nr_03_config_mode_; }
    void apply_nr_03_config_mode_transition(uint8_t low3);

    // VHDL nr_04_romram_bank (zxnext.vhd:1104 default 0x00, written by NR 0x04).
    // Selects the 8 KB SRAM bank that CPU accesses to 0x0000-0x3FFF are routed
    // through while config_mode=1 (see zxnext.vhd:3044-3050). Owned by NextReg
    // so the value survives selected-register shuffling; the MMU consults the
    // Emulator-pushed mirror rather than reaching back through NextReg.
    // VHDL issue-5 boards take all 8 bits (line 5732); older boards mask bit 7
    // (line 5717). We keep the full 8 bits and let out-of-range banks fall
    // back to 0xFF reads via Ram::page_ptr() returning nullptr.
    uint8_t nr_04_romram_bank() const { return nr_04_romram_bank_; }
    void    set_nr_04_romram_bank(uint8_t v) { nr_04_romram_bank_ = v; }

    // VHDL nr_03_machine_timing (zxnext.vhd:1099 default "011") — 3-bit ULA/
    // machine timing selector updated from NR 0x03 bits[6:4] under the gate
    // at zxnext.vhd:5124 (bit 7 = 1, user_dt_lock = 0, bit 3 = 0). Composed
    // into the NR 0x03 read at zxnext.vhd:5894.
    uint8_t nr_03_machine_timing() const { return nr_03_machine_timing_; }
    void    set_nr_03_machine_timing(uint8_t v) { nr_03_machine_timing_ = v & 0x07; }

    // VHDL nr_03_user_dt_lock (zxnext.vhd:1100 default '0') — 1-bit lock state
    // XOR-toggled by every NR 0x03 write with bit 3 = 1 (zxnext.vhd:5135).
    // Once set, gates further machine-timing writes (zxnext.vhd:5124).
    // Composed as bit 3 of the NR 0x03 read (zxnext.vhd:5894).
    bool nr_03_user_dt_lock() const { return nr_03_user_dt_lock_; }
    void set_nr_03_user_dt_lock(bool v) { nr_03_user_dt_lock_ = v; }

    // VHDL nr_03_machine_type (zxnext.vhd:1103 default "011") — 3-bit machine
    // type selector updated from NR 0x03 bits[2:0] when config_mode=1 at
    // write time (zxnext.vhd:5137-5145). Composed as bits[2:0] of the NR 0x03
    // read (zxnext.vhd:5894).
    uint8_t nr_03_machine_type() const { return nr_03_machine_type_; }
    void    set_nr_03_machine_type(uint8_t v) { nr_03_machine_type_ = v & 0x07; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::array<uint8_t, 256> regs_{};
    uint8_t selected_ = 0x24;  // VHDL zxnext.vhd:4594-4596 reset default
    bool nr_03_config_mode_ = true;  // VHDL zxnext.vhd:1102 power-on default '1'
    uint8_t nr_04_romram_bank_ = 0;  // VHDL zxnext.vhd:1104 power-on default 0x00
    uint8_t nr_03_machine_timing_ = 0x03;  // VHDL zxnext.vhd:1099 "011" default
    bool    nr_03_user_dt_lock_   = false; // VHDL zxnext.vhd:1100 '0' default
    uint8_t nr_03_machine_type_   = 0x03;  // VHDL zxnext.vhd:1103 "011" default
    std::array<std::function<void(uint8_t)>, 256> write_handlers_{};
    std::array<std::function<uint8_t()>, 256> read_handlers_{};
};
