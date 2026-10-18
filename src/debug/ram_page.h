#pragma once

#include <cstdint>

#include "debug/mem_space.h"

/// The page value that means "not a RAM page": code fetched from a ROM-mapped
/// slot or from an overlay (boot ROM, Multiface, DivMMC, Layer 2 read mapping),
/// where the MMU's page number does not name what supplied the instruction.
/// The Next has 224 8K RAM pages (0..0xDF), so 0xFF never names one.
inline constexpr uint8_t NOT_RAM_PAGE = 0xFF;

/// The page-keyed view of a fetch source (GH #339): the MMU RAM page when the
/// fetch came from `MemSpace::Page`, else NOT_RAM_PAGE. The source map, the
/// trace and the call-stack tracker are still keyed by page number (they move
/// to the full `MemSpace` in their own sub-issues of epic #23); this is the one
/// bridge, so no consumer re-derives "is it RAM" from the kind.
constexpr uint8_t ram_page_of(jnext::dbg::MemSpace s) {
    return s.kind == jnext::dbg::MemSpace::Kind::Page ? static_cast<uint8_t>(s.index)
                                                      : NOT_RAM_PAGE;
}
