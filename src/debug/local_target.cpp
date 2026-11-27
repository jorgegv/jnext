// ---------------------------------------------------------------------------
// jnext::dbg::LocalTarget - see local_target.h.
//
// The bodies are the machine halves of the verbs of debugger_inspect.cpp, moved
// here unchanged; the comments that explain WHY travel with the code.
// ---------------------------------------------------------------------------

#include "debug/local_target.h"

#include "debug/debug_state.h"
#include "memory/mmu.h"
#include "peripheral/divmmc.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include "video/palette.h"
#include "video/sprites.h"
#include "version.h"

namespace jnext {
namespace dbg {
namespace {

/// Bytes in one NR 0x50-0x57 page. A `Page` read or write that runs past the
/// end of the page is SHORT rather than wrapping into the next one: the spaces
/// are separate stores, and silently continuing into a neighbour is the bug this
/// bound exists to prevent.
constexpr uint32_t PAGE_BYTES = 0x2000;

}  // namespace

bool apply_register(Z80Registers& r, RegId reg, uint16_t value, long long* old_value,
                    const char** name) {
    const uint8_t lo = static_cast<uint8_t>(value & 0xFF);
    long long old = 0;
    const char* nm = "?";

    auto hi_of = [](uint16_t v) { return static_cast<uint16_t>(v >> 8); };
    auto lo_of = [](uint16_t v) { return static_cast<uint16_t>(v & 0xFF); };
    auto set_hi = [](uint16_t& pair, uint8_t v) {
        pair = static_cast<uint16_t>((pair & 0x00FF) | (static_cast<uint16_t>(v) << 8));
    };
    auto set_lo = [](uint16_t& pair, uint8_t v) {
        pair = static_cast<uint16_t>((pair & 0xFF00) | v);
    };

    switch (reg) {
        // ── 16-bit pairs ────────────────────────────────────────────────────
        case RegId::AF:  old = r.AF;  r.AF  = value; nm = "AF";  break;
        case RegId::BC:  old = r.BC;  r.BC  = value; nm = "BC";  break;
        case RegId::DE:  old = r.DE;  r.DE  = value; nm = "DE";  break;
        case RegId::HL:  old = r.HL;  r.HL  = value; nm = "HL";  break;
        case RegId::AF2: old = r.AF2; r.AF2 = value; nm = "AF'"; break;
        case RegId::BC2: old = r.BC2; r.BC2 = value; nm = "BC'"; break;
        case RegId::DE2: old = r.DE2; r.DE2 = value; nm = "DE'"; break;
        case RegId::HL2: old = r.HL2; r.HL2 = value; nm = "HL'"; break;
        case RegId::IX:  old = r.IX;  r.IX  = value; nm = "IX";  break;
        case RegId::IY:  old = r.IY;  r.IY  = value; nm = "IY";  break;
        case RegId::SP:  old = r.SP;  r.SP  = value; nm = "SP";  break;
        case RegId::PC:
            old = r.PC;
            r.PC = value;
            nm = "PC";
            // §4.2a, an obligation on THIS implementation rather than a property
            // of the code: setting PC must clear `halted`. Nothing in the tree
            // did it, and without it a frontend that redirects PC out of a HALT
            // leaves the CPU parked at the new address.
            r.halted = false;
            break;

        // ── 8-bit halves ────────────────────────────────────────────────────
        case RegId::A:   old = hi_of(r.AF);  set_hi(r.AF,  lo); nm = "A";   break;
        case RegId::F:   old = lo_of(r.AF);  set_lo(r.AF,  lo); nm = "F";   break;
        case RegId::B:   old = hi_of(r.BC);  set_hi(r.BC,  lo); nm = "B";   break;
        case RegId::C:   old = lo_of(r.BC);  set_lo(r.BC,  lo); nm = "C";   break;
        case RegId::D:   old = hi_of(r.DE);  set_hi(r.DE,  lo); nm = "D";   break;
        case RegId::E:   old = lo_of(r.DE);  set_lo(r.DE,  lo); nm = "E";   break;
        case RegId::H:   old = hi_of(r.HL);  set_hi(r.HL,  lo); nm = "H";   break;
        case RegId::L:   old = lo_of(r.HL);  set_lo(r.HL,  lo); nm = "L";   break;
        case RegId::A2:  old = hi_of(r.AF2); set_hi(r.AF2, lo); nm = "A'";  break;
        case RegId::F2:  old = lo_of(r.AF2); set_lo(r.AF2, lo); nm = "F'";  break;
        case RegId::B2:  old = hi_of(r.BC2); set_hi(r.BC2, lo); nm = "B'";  break;
        case RegId::C2:  old = lo_of(r.BC2); set_lo(r.BC2, lo); nm = "C'";  break;
        case RegId::D2:  old = hi_of(r.DE2); set_hi(r.DE2, lo); nm = "D'";  break;
        case RegId::E2:  old = lo_of(r.DE2); set_lo(r.DE2, lo); nm = "E'";  break;
        case RegId::H2:  old = hi_of(r.HL2); set_hi(r.HL2, lo); nm = "H'";  break;
        case RegId::L2:  old = lo_of(r.HL2); set_lo(r.HL2, lo); nm = "L'";  break;
        case RegId::IXH: old = hi_of(r.IX);  set_hi(r.IX,  lo); nm = "IXH"; break;
        case RegId::IXL: old = lo_of(r.IX);  set_lo(r.IX,  lo); nm = "IXL"; break;
        case RegId::IYH: old = hi_of(r.IY);  set_hi(r.IY,  lo); nm = "IYH"; break;
        case RegId::IYL: old = lo_of(r.IY);  set_lo(r.IY,  lo); nm = "IYL"; break;

        // ── the rest ────────────────────────────────────────────────────────
        case RegId::I:    old = r.I;    r.I    = lo;        nm = "I";    break;
        case RegId::R:    old = r.R;    r.R    = lo;        nm = "R";    break;
        case RegId::IFF1: old = r.IFF1; r.IFF1 = (lo != 0); nm = "IFF1"; break;
        case RegId::IFF2: old = r.IFF2; r.IFF2 = (lo != 0); nm = "IFF2"; break;
        case RegId::IM:   old = r.IM;   r.IM   = lo & 0x03; nm = "IM";   break;

        case RegId::Count:
            return false;   // not a register; see RegId::Count
    }
    if (old_value) *old_value = old;
    if (name) *name = nm;
    return true;
}

TargetInfo LocalTarget::info() const {
    TargetInfo t;
    t.kind    = TargetKind::Local;
    t.name    = "jnext";
    t.version = JNEXT_VERSION_STRING;
    t.link    = "local";
    return t;
}

RunState LocalTarget::run_state() const {
    const DebugState& ds = emu_.debug_state();
    RunState st;
    st.paused = ds.paused();
    st.cycle  = emu_.clock().get();
    // F2: the CURRENT frame's tag, see `frame_tag()` in debugger_impl.h.
    const uint32_t raw = emu_.frame_num();
    st.frame  = raw > 0 ? raw - 1 : 0;
    st.pc     = emu_.cpu().get_registers().PC;

    // The published mirror of the internal enum. `debug_types_check.cpp` pins
    // the two value by value, which is what makes this switch a relabelling
    // rather than a mapping that could be wrong.
    switch (ds.step_mode()) {
        case ::StepMode::NONE:              st.step_mode = StepMode::None;           break;
        case ::StepMode::INTO:              st.step_mode = StepMode::Into;           break;
        case ::StepMode::OVER:              st.step_mode = StepMode::Over;           break;
        case ::StepMode::OUT:               st.step_mode = StepMode::Out;            break;
        case ::StepMode::RUN_TO_CYCLE:      st.step_mode = StepMode::RunToCycle;     break;
        case ::StepMode::STEP_BACK:         st.step_mode = StepMode::StepBack;       break;
        case ::StepMode::RUN_BACK_TO_CYCLE: st.step_mode = StepMode::RunBackToCycle; break;
    }
    return st;
}

Result LocalTarget::step_instruction() {
    // GH #207 - debugger_step(), not the raw execute_single_instruction()
    // primitive: while the debugger holds the machine nothing else calls
    // run_frame(), so the Step has to turn frames over too, and a Step at a HALT
    // has to run the halt out (the CPU leaves it only on an accepted interrupt).
    emu_.debugger_step();
    return Result::Ok;
}

Z80Registers LocalTarget::registers() const { return emu_.cpu().get_registers(); }

Result LocalTarget::set_register(RegId reg, uint16_t value) {
    Z80Registers r = emu_.cpu().get_registers();
    if (!apply_register(r, reg, value, nullptr, nullptr)) return Result::Unsupported;
    emu_.cpu().set_registers(r);
    return Result::Ok;
}

Expected<size_t> LocalTarget::read(MemSpace space, uint32_t addr, size_t n, uint8_t* buf) const {
    Emulator& emu = emu_;
    switch (space.kind) {
        case MemSpace::Kind::Cpu: {
            for (size_t i = 0; i < n; ++i)
                buf[i] = emu.mmu().peek(static_cast<uint16_t>((addr + i) & 0xFFFF));
            return make_ok<size_t>(n);
        }
        case MemSpace::Kind::Page: {
            // TWO guards, and each one has a row that bites (review of B1 found
            // three copies of one condition here, of which two were unreachable
            // and none was distinguishable by a test):
            //
            //   1. an index outside the 8-bit page-number space at all. Without
            //      this, the narrowing cast below turns 0x100 into page 0 and a
            //      nonsense request reads real memory (rows INS-02-16/17).
            //   2. `nr_page_ptr()` == nullptr, which is EVERY page >= 0xE0:
            //      `mmu_A21_A13(8)='1'` -> `sram_pre_active='0'`
            //      (zxnext.vhd:3061), the SRAM does not respond, so there is no
            //      backing store to hand out. The two ROM sentinels 0xFE / 0xFF
            //      are inside that band and are refused BY IT (rows INS-02-06/07
            //      for the sentinels, INS-02-13 for 0xE0 and INS-02-14 for 0xFD,
            //      which are the band and NOT sentinels, and INS-02-15 for 0xDF,
            //      the top page that is still valid).
            if (space.index > 0xFF) return make_refused<size_t>(Result::InvalidPage);
            const uint8_t* p = emu.mmu().nr_page_ptr(static_cast<uint8_t>(space.index));
            if (!p) return make_refused<size_t>(Result::InvalidPage);
            // A page is 8 KB. A read that runs past its end is SHORT, not an
            // error: `Expected<size_t>` reports what it managed (INS-02).
            size_t done = 0;
            for (; done < n && addr + done < PAGE_BYTES; ++done)
                buf[done] = p[addr + done];
            return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
        }
        case MemSpace::Kind::Rom: {
            const uint8_t* p = emu.mmu().rom_image_ptr(static_cast<uint8_t>(space.index));
            if (!p) return make_refused<size_t>(Result::InvalidPage);
            size_t done = 0;
            for (; done < n && addr + done < 0x4000; ++done)
                buf[done] = p[addr + done];
            return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
        }
        // GH #351 (E23-21): a DivMMC RAM bank, read-only here (`poke` refuses it). Bank
        // b is SRAM page 0x10 + b on the Next (zxnext.vhd:3092-3098), the page
        // `DivMmc::ram_page()` returns whatever E3 selects now. Side-effect free.
        case MemSpace::Kind::DivMmcRam: {
            if (space.index >= static_cast<unsigned>(DivMmc::kRamPages))
                return make_refused<size_t>(Result::InvalidPage);
            const uint8_t* p = emu.divmmc().ram_page(space.index);
            size_t done = 0;
            for (; done < n && addr + done < static_cast<size_t>(DivMmc::kRamPageSize); ++done)
                buf[done] = p[addr + done];
            return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
        }
        // The identity-only kinds (GH #339): named by the read arbiter, accessed
        // by no verb yet. Listed one by one (no `default`) so `-Werror=switch`
        // flags the next kind added to `MemSpace::Kind`.
        case MemSpace::Kind::BootRom:
        case MemSpace::Kind::MultifaceRom:
        case MemSpace::Kind::MultifaceRam:
        case MemSpace::Kind::DivMmcRom:
        case MemSpace::Kind::AltRom:
        case MemSpace::Kind::Layer2:
        case MemSpace::Kind::Config:
        case MemSpace::Kind::Floating:
            return make_refused<size_t>(Result::Unsupported);
    }
    return make_refused<size_t>(Result::Unsupported);
}

Expected<size_t> LocalTarget::write(MemSpace space, uint32_t addr, size_t n, const uint8_t* buf) {
    Emulator& emu = emu_;
    size_t done = 0;
    if (space.kind == MemSpace::Kind::Cpu) {
        // §4.2a: `Mmu::write` - through the live map, overlays honoured, ROM
        // ignored, the per-scanline change logs and the attribute mux updated,
        // no latch, no event. Exactly the Memory panel's path, because it is the
        // same function the Memory panel calls.
        //
        // GH #281 (F1) - AND IT COUNTS WHAT LANDED. Each byte goes through
        // `Mmu::write_landed()`, which IS `Mmu::write` returning its own routing
        // decision, so a byte an overlay takes (Layer 2 write-over, DivMMC or
        // Multiface RAM) counts and a byte dropped on ROM does not - decided by
        // the one copy of the rules, never re-derived here. Every byte is still
        // offered, as the CPU's own write would be: a range straddling ROM and
        // RAM lands its RAM bytes (DZRP relies on that). The count is what
        // landed; `Ok` iff all of them did, `RefusedReadOnly` otherwise - with 0
        // when none did (CAP-INS-02, REQ-gdb-6).
        //
        // GH #276 B2 - UNDER `InspectionScope`, the write half of what
        // `Mmu::peek()` does for reads (B1's F1): a script mutation runs AT A
        // DELIVERY, inside `run_frame()`'s `GuestExecutionScope`, where
        // `watchpoints_live()` is true, so without this a handler's `poke` would
        // latch a watch on itself and the machine would stop on the debugger's
        // own write. The drain wraps every handler in one scope already, so this
        // is the SECOND of two guards, deliberately: it makes the property hold
        // for any caller on any path (it nests and restores). `EVT-DEL-05/06`
        // pin the drain's scope, which is the one that is load-bearing.
        DebugState::InspectionScope scope(emu.debug_state());
        size_t landed = 0;
        for (; done < n; ++done)
            if (emu.mmu().write_landed(static_cast<uint16_t>((addr + done) & 0xFFFF), buf[done]))
                ++landed;
        return Expected<size_t>{landed == n ? Result::Ok : Result::RefusedReadOnly, landed};
    }

    // Page: same two guards as `read`, in the same order and for the same reasons.
    if (space.index > 0xFF) return make_refused<size_t>(Result::InvalidPage);
    uint8_t* p = emu.mmu().nr_page_ptr(static_cast<uint8_t>(space.index));
    if (!p) return make_refused<size_t>(Result::InvalidPage);
    // GH #362 - a poke of a page's backing byte is a write like any other
    // for the tilemap / LoRes / Layer 2 beam replays (coarse tag): log it
    // before it lands.
    const uint8_t page = static_cast<uint8_t>(space.index);
    for (; done < n && addr + done < PAGE_BYTES; ++done) {
        emu.mmu().log_page_store(page, static_cast<uint16_t>(addr + done), p + addr + done,
                                 buf[done]);
        p[addr + done] = buf[done];
    }
    return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
}

std::array<SlotInfo, 8> LocalTarget::mmu_slots() const {
    std::array<SlotInfo, 8> out{};
    const Mmu& mmu = emu_.mmu();
    for (int s = 0; s < 8; ++s) {
        SlotInfo& si = out[s];
        si.nr_page        = mmu.get_nr_page(s);
        si.effective_page = mmu.get_effective_page(s);
        si.is_rom         = mmu.is_slot_rom(s);
        // The COMPLETE address of the backing store, so no client composes one.
        // A ROM slot's effective_page is an un-shifted `ram_` page index, NOT an
        // NR page number, and handing it to `MemSpace::page()` is exactly the
        // mistake round 4 of the design review caught on two frontends at once.
        if (si.is_rom) {
            si.space        = MemSpace::rom(static_cast<uint16_t>(si.effective_page >> 1));
            si.space_offset = static_cast<uint16_t>((si.effective_page & 1) * 0x2000);
        } else {
            si.space        = MemSpace::page(si.nr_page);
            si.space_offset = 0;
        }
        si.now = mmu.route(static_cast<uint16_t>(s << 13)).space;
    }
    return out;
}

Result LocalTarget::set_mmu_slot(int slot, uint8_t page) {
    // Through the NR 0x50-0x57 WRITE HANDLER, i.e. exactly what the guest's
    // own `NEXTREG 0x50+slot,page` does (GH #12): page 0xFF on slot 0/1
    // re-engages legacy ROM paging there (zxnext.vhd:4611-4612, :3052). A bare
    // `Mmu::set_page(slot, 0xFF)` instead left the slot UNMAPPED - reads 0xFF,
    // writes dropped - which is what DZRP's CMD_SET_SLOT 0,0xFF, sent for slots
    // 0/1 on every DeZog .sna/.z80/.nex load, then did to the ROM.
    emu_.nextreg().write(static_cast<uint8_t>(0x50 + slot), page);
    return Result::Ok;
}

// GH #12 - the ROM image legacy paging selects: `Mmu::current_sram_rom()`, which
// is VHDL `sram_rom` with the machine type and the NR 0x8C locks already folded in.
MemSpace LocalTarget::rom_select() const {
    return MemSpace::rom(emu_.mmu().current_sram_rom());
}

uint8_t LocalTarget::nextreg_read(uint8_t reg) const {
    // peek(), never read(): read() runs the register's read handler, which for
    // NR 0x2C/0x2E latches the Pi-I2S sample the guest is about to read, and it
    // emits a trace line per read.
    return emu_.nextreg().peek(reg);
}

Result LocalTarget::nextreg_write(uint8_t reg, uint8_t value) {
    // The register's own write handler runs, synchronously, side effects
    // included - INS-04. Source is the Debugger, so it fires no NextRegWrite
    // event on itself.
    emu_.nextreg().write(reg, value);
    return Result::Ok;
}

Expected<uint8_t> LocalTarget::port_in(uint16_t port) {
    return make_ok<uint8_t>(emu_.port().read(port));
}

Result LocalTarget::port_out(uint16_t port, uint8_t value) {
    emu_.port().write(port, value);
    return Result::Ok;
}

std::vector<SpriteInfo> LocalTarget::sprites() const {
    std::vector<SpriteInfo> out;
    out.reserve(SPRITE_COUNT);
    for (size_t i = 0; i < SPRITE_COUNT; ++i)
        out.push_back(emu_.sprites().get_sprite_info(static_cast<uint8_t>(i)));
    return out;
}

Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>> LocalTarget::sprite_attr_raw(uint8_t idx) const {
    using Bytes = std::array<uint8_t, SPRITE_ATTR_BYTES>;
    Bytes b{};
    for (size_t i = 0; i < SPRITE_ATTR_BYTES; ++i)
        b[i] = emu_.sprites().read_attr_byte(idx, static_cast<uint8_t>(i));
    return make_ok<Bytes>(b);
}

Result LocalTarget::set_sprite_attr_raw(uint8_t idx, const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i < n; ++i)
        emu_.sprites().write_attr_byte_at(idx, static_cast<uint8_t>(i), bytes[i]);
    return Result::Ok;
}

ConstBytes LocalTarget::pattern_ram() const {
    return ConstBytes{emu_.sprites().pattern_ram_data(), PATTERN_RAM_BYTES};
}

Result LocalTarget::write_pattern_ram(uint16_t addr, const uint8_t* src, size_t n) {
    for (size_t i = 0; i < n; ++i)
        emu_.sprites().write_pattern_byte_at(static_cast<uint16_t>(addr + i), src[i]);
    return Result::Ok;
}

Expected<uint16_t> LocalTarget::sprite_palette_rgb333(int bank, uint8_t index) const {
    const ::PaletteId id = bank ? ::PaletteId::SPRITE_SECOND : ::PaletteId::SPRITE_FIRST;
    return make_ok<uint16_t>(emu_.palette().entry_rgb333(id, index));
}

ClipWindow LocalTarget::sprite_clip() const {
    ClipWindow w;
    const SpriteEngine& s = emu_.sprites();
    w.x1 = s.clip_x1();
    w.x2 = s.clip_x2();
    w.y1 = s.clip_y1();
    w.y2 = s.clip_y2();
    return w;
}

}  // namespace dbg
}  // namespace jnext
