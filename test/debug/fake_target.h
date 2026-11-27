#pragma once

// A `jnext::dbg::Target` for tests: records every MACHINE call by name (identity
// queries are not machine calls), answers from canned values, and has the hostile
// knobs the facade must survive (a count above the request, a wrong-sized sprite
// list, a bad pattern-RAM view). Used by debug_target_test; R1.3's Qt rows and the
// remote targets' rows can reuse it.

#include <array>
#include <cstdint>
#include <cstring>
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
    std::vector<SpriteInfo> sprite_list;
    std::array<uint8_t, SPRITE_ATTR_BYTES> attr{};
    std::vector<uint8_t> pattern;
    bool             pattern_null = false;
    ClipWindow       clip{};
    uint16_t         pal_value = 0x1FF;
    /// Hostile knobs: a read or write reports `n + count_extra`; a non-zero value
    /// makes the target claim more than was asked.
    size_t           count_extra = 0;

    // ---- the record ----------------------------------------------------------
    std::vector<std::string> calls;                  ///< "name(args)" per machine call
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
        return Result::Ok;
    }

    // ---- memory --------------------------------------------------------------
    Expected<size_t> read(MemSpace space, uint32_t addr, size_t n, uint8_t* buf) const override {
        rec("read(" + std::to_string(static_cast<int>(space.kind)) + "," + std::to_string(addr) + "," +
            std::to_string(n) + ")");
        for (size_t i = 0; i < n; ++i) buf[i] = ram[(addr + i) & 0xFFFF];
        return Expected<size_t>{Result::Ok, n + count_extra};
    }
    Expected<size_t> write(MemSpace space, uint32_t addr, size_t n, const uint8_t* b) override {
        rec("write(" + std::to_string(static_cast<int>(space.kind)) + "," + std::to_string(addr) + "," +
            std::to_string(n) + ")");
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
        return Expected<uint8_t>{Result::Ok, 0xA5};
    }
    Result port_out(uint16_t port, uint8_t value) override {
        rec("port_out(" + std::to_string(port) + "," + std::to_string(value) + ")");
        return Result::Ok;
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

private:
    void rec(const std::string& s) const { const_cast<std::vector<std::string>&>(calls).push_back(s); }
};

}  // namespace dbg
}  // namespace jnext
