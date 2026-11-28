#pragma once

// A `jnext::dbg::Target` for tests: records every MACHINE call by name (identity
// queries are not machine calls), answers from canned values, and has the hostile
// knobs the facade must survive (a count above the request, a wrong-sized sprite
// list, a bad pattern-RAM view). Used by debug_target_test; R1.3's Qt rows and the
// remote targets' rows can reuse it.

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "debug/target.h"

namespace jnext {
namespace dbg {

class FakeTarget final : public Target {
public:
    // ---- configuration -------------------------------------------------------
    TargetInfo       info_v;
    CapabilitySet    caps = CapabilitySet::all();
    std::string      why_text;                       ///< why_unavailable() of a missing bit
    RegisterValidity validity = RegisterValidity::all();
    RunState         run;
    Z80Registers     regs;
    std::array<SlotInfo, 8> slots{};
    MemSpace         rom = MemSpace::rom(3);
    uint8_t          nr[256] = {};
    uint8_t          ram[65536] = {};                ///< the Cpu space and every Page, flat
    /// A page that has bytes here is read from them (8 KB, `Page{p}` reads at an offset in
    /// the page); any other read is the flat `ram`. GH #519.
    std::map<uint8_t, std::vector<uint8_t>> page_ram;
    std::vector<SpriteInfo> sprite_list;
    std::array<uint8_t, SPRITE_ATTR_BYTES> attr{};
    std::vector<uint8_t> pattern;
    bool             pattern_null = false;
    ClipWindow       clip{};
    uint16_t         pal_value = 0x1FF;
    /// Hostile knobs: a read or write reports `n + count_extra`; a non-zero value
    /// makes the target claim more than was asked.
    size_t           count_extra = 0;
    /// Failure knobs: a read reports `short_by` bytes fewer than asked under Ok; a
    /// non-Ok `write_status` makes write() fail (count 0, nothing stored); a non-Ok
    /// `set_register_status` / `port_in_status` / `port_out_status` makes that verb fail.
    size_t           short_by = 0;
    Result           write_status = Result::Ok;
    Result           set_register_status = Result::Ok;
    Result           port_in_status = Result::Ok;
    Result           port_out_status = Result::Ok;
    /// TGT-05: a non-Ok status makes that verb fail (nothing recorded as held).
    Result           resume_status = Result::Ok;
    Result           pause_status = Result::Ok;
    Result           add_bp_status = Result::Ok;
    Result           add_wp_status = Result::Ok;
    Result           remove_bp_status = Result::Ok;
    Result           remove_wp_status = Result::Ok;
    /// TGT-05: the stops the target will report, oldest first. `take_stop()` hands one
    /// out and, as a real target's stop does, leaves the machine paused. It is a cheap
    /// query like `run_state()`: not a recorded call.
    std::vector<TargetStop> stops;
    /// The handle the next `add_breakpoint` / `add_watchpoint` returns.
    uint32_t         next_handle = 1;

    // ---- the record ----------------------------------------------------------
    std::vector<std::string> calls;                  ///< "name(args)" per machine call
    /// Every read() with its space, which `calls` does not name. GH #519.
    struct ReadRec { MemSpace space; uint32_t addr; size_t n; };
    mutable std::vector<ReadRec> reads;
    int count(const char* name) const {
        int n = 0;
        const size_t len = std::strlen(name);
        for (const auto& c : calls)
            if (c.compare(0, len, name) == 0 && (c.size() == len || c[len] == '(')) ++n;
        return n;
    }
    bool called(const char* name) const { return count(name) > 0; }
    void clear() { calls.clear(); }

    FakeTarget() {
        info_v.kind = TargetKind::Remote;
        info_v.name = "fake";
        info_v.version = "0";
        info_v.link = "test";
        for (size_t i = 0; i < SPRITE_COUNT; ++i) sprite_list.emplace_back();
        pattern.assign(PATTERN_RAM_BYTES, 0);
        std::memset(ram, 0, sizeof(ram));
        for (int s = 0; s < 8; ++s) {
            slots[s].nr_page = static_cast<uint8_t>(0x20 + s);
            slots[s].effective_page = slots[s].nr_page;
            slots[s].space = MemSpace::page(slots[s].nr_page);
        }
        run.paused = true;
        run.cycle = 1234;
        run.frame = 7;
        run.pc = 0x4321;
        regs.PC = 0x4321;
        regs.AF = 0x1122;
    }

    // ---- identity ------------------------------------------------------------
    TargetInfo       info() const override { return info_v; }
    CapabilitySet    capabilities() const override { return caps; }
    std::string      why_unavailable(Capability c) const override {
        return caps.has(c) ? std::string() : why_text;
    }
    RegisterValidity register_validity() const override { return validity; }

    // ---- run state -----------------------------------------------------------
    RunState run_state() const override { return run; }
    Result   step_instruction() override { rec("step_instruction"); return Result::Ok; }

    // ---- registers -----------------------------------------------------------
    Z80Registers registers() const override { rec("registers"); return regs; }
    Result set_register(RegId reg, uint16_t value) override {
        rec("set_register(" + std::to_string(static_cast<int>(reg)) + "," + std::to_string(value) + ")");
        return set_register_status;
    }

    // ---- memory --------------------------------------------------------------
    Expected<size_t> read(MemSpace space, uint32_t addr, size_t n, uint8_t* buf) const override {
        rec("read(" + std::to_string(static_cast<int>(space.kind)) + "," + std::to_string(addr) + "," +
            std::to_string(n) + ")");
        reads.push_back({space, addr, n});
        const auto pr = space.kind == MemSpace::Kind::Page ? page_ram.find(static_cast<uint8_t>(space.index))
                                                           : page_ram.end();
        for (size_t i = 0; i < n; ++i)
            buf[i] = pr != page_ram.end() ? pr->second[(addr + i) % pr->second.size()]
                                          : ram[(addr + i) & 0xFFFF];
        return Expected<size_t>{Result::Ok, (n >= short_by ? n - short_by : 0) + count_extra};
    }
    Expected<size_t> write(MemSpace space, uint32_t addr, size_t n, const uint8_t* b) override {
        rec("write(" + std::to_string(static_cast<int>(space.kind)) + "," + std::to_string(addr) + "," +
            std::to_string(n) + ")");
        if (write_status != Result::Ok) return Expected<size_t>{write_status, 0};
        for (size_t i = 0; i < n; ++i) ram[(addr + i) & 0xFFFF] = b[i];
        return Expected<size_t>{Result::Ok, n + count_extra};
    }

    // ---- banking -------------------------------------------------------------
    std::array<SlotInfo, 8> mmu_slots() const override { rec("mmu_slots"); return slots; }
    Result set_mmu_slot(int slot, uint8_t page) override {
        rec("set_mmu_slot(" + std::to_string(slot) + "," + std::to_string(page) + ")");
        return Result::Ok;
    }
    MemSpace rom_select() const override { rec("rom_select"); return rom; }

    // ---- NextREG and ports ---------------------------------------------------
    uint8_t nextreg_read(uint8_t reg) const override {
        rec("nextreg_read(" + std::to_string(reg) + ")");
        return nr[reg];
    }
    Result nextreg_write(uint8_t reg, uint8_t value) override {
        rec("nextreg_write(" + std::to_string(reg) + "," + std::to_string(value) + ")");
        nr[reg] = value;
        return Result::Ok;
    }
    Expected<uint8_t> port_in(uint16_t port) override {
        rec("port_in(" + std::to_string(port) + ")");
        return Expected<uint8_t>{port_in_status, 0xA5};
    }
    Result port_out(uint16_t port, uint8_t value) override {
        rec("port_out(" + std::to_string(port) + "," + std::to_string(value) + ")");
        return port_out_status;
    }

    // ---- sprites -------------------------------------------------------------
    std::vector<SpriteInfo> sprites() const override { rec("sprites"); return sprite_list; }
    Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>> sprite_attr_raw(uint8_t idx) const override {
        rec("sprite_attr_raw(" + std::to_string(idx) + ")");
        return Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>>{Result::Ok, attr};
    }
    Result set_sprite_attr_raw(uint8_t idx, const uint8_t*, size_t n) override {
        rec("set_sprite_attr_raw(" + std::to_string(idx) + "," + std::to_string(n) + ")");
        return Result::Ok;
    }
    ConstBytes pattern_ram() const override {
        rec("pattern_ram");
        return ConstBytes{pattern_null ? nullptr : pattern.data(), pattern.size()};
    }
    Result write_pattern_ram(uint16_t addr, const uint8_t*, size_t n) override {
        rec("write_pattern_ram(" + std::to_string(addr) + "," + std::to_string(n) + ")");
        return Result::Ok;
    }
    Expected<uint16_t> sprite_palette_rgb333(int bank, uint8_t index) const override {
        rec("sprite_palette_rgb333(" + std::to_string(bank) + "," + std::to_string(index) + ")");
        return Expected<uint16_t>{Result::Ok, pal_value};
    }
    ClipWindow sprite_clip() const override { rec("sprite_clip"); return clip; }

    // ---- run control, breakpoints, watchpoints (TGT-05) ----------------------
    Result resume() override {
        rec("resume");
        if (resume_status != Result::Ok) return resume_status;
        run.paused = false;
        return Result::Ok;
    }
    Result pause() override {
        rec("pause");
        if (pause_status != Result::Ok) return pause_status;
        run.paused = true;
        return Result::Ok;
    }
    bool take_stop(TargetStop& out) override {
        if (stops.empty()) return false;
        out = stops.front();
        stops.erase(stops.begin());
        run.paused = true;
        return true;
    }
    Expected<uint32_t> add_breakpoint(uint16_t addr, uint16_t page) override {
        rec("add_breakpoint(" + std::to_string(addr) + "," + std::to_string(page) + ")");
        if (add_bp_status != Result::Ok) return make_refused<uint32_t>(add_bp_status);
        return make_ok<uint32_t>(next_handle++);
    }
    Result remove_breakpoint(uint32_t handle) override {
        rec("remove_breakpoint(" + std::to_string(handle) + ")");
        return remove_bp_status;
    }
    Expected<uint32_t> add_watchpoint(uint16_t lo, uint16_t hi, uint16_t page, Access access) override {
        rec("add_watchpoint(" + std::to_string(lo) + "," + std::to_string(hi) + "," +
            std::to_string(page) + "," + std::to_string(static_cast<int>(access)) + ")");
        if (add_wp_status != Result::Ok) return make_refused<uint32_t>(add_wp_status);
        return make_ok<uint32_t>(next_handle++);
    }
    Result remove_watchpoint(uint32_t handle) override {
        rec("remove_watchpoint(" + std::to_string(handle) + ")");
        return remove_wp_status;
    }

private:
    void rec(const std::string& s) const { const_cast<std::vector<std::string>&>(calls).push_back(s); }
};

}  // namespace dbg
}  // namespace jnext
