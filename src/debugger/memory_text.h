#pragma once

// The long address in the Qt views (GH #360, E23-22): the text each panel prints
// for "which memory backs this address", through the one formatter
// (`debug/long_address.h`). A panel holds an `AddressStyle` (the Debug menu's
// "Physical Addresses" toggle, handed down by the window) and nothing else.

#include <QString>

#include "debug/debugger.h"
#include "debug/long_address.h"

namespace memtext {

using jnext::dbg::AddressStyle;
using jnext::dbg::MemSpace;

/// The Next's memory layout is what the Physical style prints; any other
/// machine renders the Friendly tag (plan D3).
inline bool next_layout(const jnext::dbg::Debugger* dbg) {
    return dbg && dbg->machine().type == MachineType::ZXN_ISSUE2;
}

/// "" for the CPU's own map, otherwise the tag or the physical text of `addr` in `s`.
inline QString text(const jnext::dbg::Debugger* dbg, AddressStyle st, MemSpace s, uint16_t addr) {
    return QString::fromStdString(jnext::dbg::space_text(s, addr, st, next_layout(dbg)));
}

/// What a read of `addr` reaches NOW (the slot's `SlotInfo::now`).
inline MemSpace now_of(const std::array<jnext::dbg::SlotInfo, 8>& sinfo, uint16_t addr) {
    return sinfo[addr >> 13].now;
}

}  // namespace memtext
