#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg::LocalTarget - the `Target` over the in-process `Emulator`.
//
// Epic #486, R1.1 (TGT-01). INTERNAL to `jnext_debug`, like `debugger_impl.h`:
// it includes `core/emulator.h` on purpose and is NOT one of the published
// headers the include-graph lint checks. `Debugger(Emulator&)` builds and owns
// one; nothing else constructs it.
//
// Every method is the post-guard ACTION of a facade verb, moved verbatim from the
// body the facade had: the facade keeps the argument validation, the RZX and
// inside-delivery refusals and the MUTATE attribution, in today's order.
//
// It holds the `Emulator&` and NOTHING derived from it: a CTL-12 hard reset
// destroys and placement-news the `Emulator` in place, so a cached `Mmu&` or
// sprite-engine pointer would dangle. Every method fetches its sub-object per
// call.
// ---------------------------------------------------------------------------

#include "core/emulator.h"
#include "debug/target.h"

namespace jnext {
namespace dbg {

/// One register edit, shared by the facade (which only wants the OLD value and
/// the NAME for its MUTATE line, from a copy) and `LocalTarget::set_register`
/// (which writes the result back). Returns false for `RegId::Count`.
/// `value` is the raw argument; 8-bit ids take its low byte. `PC` clears `halted`.
bool apply_register(Z80Registers& r, RegId reg, uint16_t value, long long* old_value,
                    const char** name);

class LocalTarget final : public Target {
public:
    explicit LocalTarget(Emulator& emu) : emu_(emu) {}

    TargetInfo       info() const override;
    CapabilitySet    capabilities() const override { return CapabilitySet::all(); }
    std::string      why_unavailable(Capability) const override { return std::string(); }
    RegisterValidity register_validity() const override { return RegisterValidity::all(); }

    RunState run_state() const override;
    Result   step_instruction() override;

    Z80Registers registers() const override;
    Result       set_register(RegId reg, uint16_t value) override;

    Expected<size_t> read(MemSpace space, uint32_t addr, size_t n, uint8_t* buf) const override;
    Expected<size_t> write(MemSpace space, uint32_t addr, size_t n, const uint8_t* buf) override;

    std::array<SlotInfo, 8> mmu_slots() const override;
    Result                  set_mmu_slot(int slot, uint8_t page) override;
    MemSpace                rom_select() const override;

    uint8_t           nextreg_read(uint8_t reg) const override;
    Result            nextreg_write(uint8_t reg, uint8_t value) override;
    Expected<uint8_t> port_in(uint16_t port) override;
    Result            port_out(uint16_t port, uint8_t value) override;

    std::vector<SpriteInfo> sprites() const override;
    Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>> sprite_attr_raw(uint8_t idx) const override;
    Result                  set_sprite_attr_raw(uint8_t idx, const uint8_t* bytes, size_t n) override;
    ConstBytes              pattern_ram() const override;
    Result                  write_pattern_ram(uint16_t addr, const uint8_t* src, size_t n) override;
    Expected<uint16_t>      sprite_palette_rgb333(int bank, uint8_t index) const override;
    ClipWindow              sprite_clip() const override;

    // TGT-05: run control and breakpoints are the local facade's own (`DebugState`,
    // `EventTable`), which never calls these. They exist for the interface.
    Result resume() override { return Result::Unsupported; }
    Result pause() override { return Result::Unsupported; }
    bool   take_stop(TargetStop&) override { return false; }
    Expected<uint32_t> add_breakpoint(uint16_t, uint16_t) override {
        return make_refused<uint32_t>(Result::Unsupported);
    }
    Result remove_breakpoint(uint32_t) override { return Result::Unsupported; }
    Expected<uint32_t> add_watchpoint(uint16_t, uint16_t, uint16_t, Access) override {
        return make_refused<uint32_t>(Result::Unsupported);
    }
    Result remove_watchpoint(uint32_t) override { return Result::Unsupported; }

private:
    Emulator& emu_;
};

}  // namespace dbg
}  // namespace jnext
