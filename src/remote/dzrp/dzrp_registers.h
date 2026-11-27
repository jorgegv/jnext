#pragma once

// ---------------------------------------------------------------------------
// DZRP's register numbers, in ONE place: the server (CMD_SET_REGISTER) and the
// remote target (`DzrpTarget::set_register`) both read this table.
// ---------------------------------------------------------------------------

#include <cstddef>

#include "debug/inspect.h"

namespace jnext {
namespace remote {
namespace dzrp {

/// `CMD_SET_REGISTER`'s register numbers (spec: "0=PC, 1=SP, 2=AF, 3=BC, 4=DE,
/// 5=HL, 6=IX, 7=IY, 8=AF', 9=BC', 10=DE', 11=HL', 13=IM, 14=F, 15=A, 16=C,
/// 17=B, 18=E, 19=D, 20=L, 21=H, 22=IXL, 23=IXH, 24=IYL, 25=IYH, 26=F', 27=A',
/// 28=C', 29=B', 30=E', 31=D', 32=L', 33=H', 34=R, 35=I") → the backend's
/// `RegId`. VERIFIED equal to DeZog's own `Z80_REG` enum (`z80registers.ts:14-25`,
/// 3.7.4 and upstream 3.8.0-rc7 alike), which is what DeZog sends. The one
/// difference: DeZog names 12 `IR` (I<<8 | R) where the spec says 12 is unused;
/// DeZog's register panes never offer IR for editing, so only the raw debug
/// console (`-dbg cmd_set_register 12 …`) can send it, and it is refused as the
/// spec's "unused" (design §2 row 4: unknown index → warn).
constexpr int kDzrpNoReg = -1;
constexpr int kDzrpRegMap[] = {
    static_cast<int>(jnext::dbg::RegId::PC),  static_cast<int>(jnext::dbg::RegId::SP),  static_cast<int>(jnext::dbg::RegId::AF),
    static_cast<int>(jnext::dbg::RegId::BC),  static_cast<int>(jnext::dbg::RegId::DE),  static_cast<int>(jnext::dbg::RegId::HL),
    static_cast<int>(jnext::dbg::RegId::IX),  static_cast<int>(jnext::dbg::RegId::IY),  static_cast<int>(jnext::dbg::RegId::AF2),
    static_cast<int>(jnext::dbg::RegId::BC2), static_cast<int>(jnext::dbg::RegId::DE2), static_cast<int>(jnext::dbg::RegId::HL2),
    kDzrpNoReg,                       static_cast<int>(jnext::dbg::RegId::IM),  static_cast<int>(jnext::dbg::RegId::F),
    static_cast<int>(jnext::dbg::RegId::A),   static_cast<int>(jnext::dbg::RegId::C),   static_cast<int>(jnext::dbg::RegId::B),
    static_cast<int>(jnext::dbg::RegId::E),   static_cast<int>(jnext::dbg::RegId::D),   static_cast<int>(jnext::dbg::RegId::L),
    static_cast<int>(jnext::dbg::RegId::H),   static_cast<int>(jnext::dbg::RegId::IXL), static_cast<int>(jnext::dbg::RegId::IXH),
    static_cast<int>(jnext::dbg::RegId::IYL), static_cast<int>(jnext::dbg::RegId::IYH), static_cast<int>(jnext::dbg::RegId::F2),
    static_cast<int>(jnext::dbg::RegId::A2),  static_cast<int>(jnext::dbg::RegId::C2),  static_cast<int>(jnext::dbg::RegId::B2),
    static_cast<int>(jnext::dbg::RegId::E2),  static_cast<int>(jnext::dbg::RegId::D2),  static_cast<int>(jnext::dbg::RegId::L2),
    static_cast<int>(jnext::dbg::RegId::H2),  static_cast<int>(jnext::dbg::RegId::R),   static_cast<int>(jnext::dbg::RegId::I),
};
constexpr std::size_t kDzrpRegCount = sizeof(kDzrpRegMap) / sizeof(kDzrpRegMap[0]);
static_assert(kDzrpRegCount == 36, "DZRP register numbers run 0..35");

/// The `RegId` (as an int) of DZRP register number `index`, `kDzrpNoReg` for the unused
/// 12 and anything past 35.
inline int dzrp_reg_of(std::size_t index) {
    return index < kDzrpRegCount ? kDzrpRegMap[index] : kDzrpNoReg;
}

/// DZRP register number of `reg`, -1 when DZRP has none (IFF1, IFF2: CMD_INTERRUPT_ON_OFF
/// sets both).
inline int dzrp_index_of(jnext::dbg::RegId reg) {
    for (std::size_t i = 0; i < kDzrpRegCount; ++i)
        if (kDzrpRegMap[i] == static_cast<int>(reg)) return static_cast<int>(i);
    return -1;
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
