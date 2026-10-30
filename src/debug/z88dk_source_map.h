#pragma once

#include "debug/sld_loader.h"
#include "debug/symbol_table.h"

#include <string>

class SourceMap;

/// Load the source half of a z88dk `-debug` map (design 9.4, GH #344) into a
/// compiler-neutral SourceMap: the `__C_LINE_` and `__ASM_LINE_` records become
/// ranges, each in the 8K page its section names (`z88dk_section_space()`), or
/// for a section that names none (rule 4), the page `space_of` says supplies the
/// address now.
///
/// Strict, like the SLD half: a record that cannot be placed (an unparsable
/// location, a value over 24 bits, a DivMMC / out-of-range / head-less section,
/// a rule-4 address outside RAM) refuses the half, `count == -1`, the reason
/// naming the map line, and the store is untouched. A map with no line record
/// (built without -debug, or every line in an empty section) gives `count == 0`,
/// no error, and leaves the store untouched. On success `count` is the number
/// of ranges stored.
///
/// Extents: to the next line start in the same space, the next `addr` symbol of
/// another module in it, the section's `__<section>_tail`, start + 0x400 or the
/// 8K page end, whichever is first. Lines sharing an address: the highest line
/// owns the range; a lower one of the same module and function is its alias,
/// else it has no code.
///
/// Identity: the section binaries `<map stem>_<GROUP>.bin` beside the map give a
/// byte check for each non-data line range they cover. No binary, no identity.
/// Data sections (data*, bss*, smc*) are never checked.
///
/// The symbols are the symbol table's (`SymbolTable::load_z88dk_map()`); this
/// fills only the SourceMap.
SourceMapLoadResult load_z88dk_source_map(SourceMap& map, const std::string& path,
                                          const SymbolSpaceOf& space_of);
