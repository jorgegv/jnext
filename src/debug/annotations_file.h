#pragma once

// ---------------------------------------------------------------------------
// The annotations project file, `.jdp` (GH #387, E23-24; design 10.5): the text
// form of a `UserAnnotations`. std streams only; the space of a record is
// `long_address.h`'s `space_tag()` / `parse_space_tag()`.
//
//   jnext-annotations 1
//   label P04 $8000 Start
//   comment P04 $8000 Entry\npoint \\ here
//
// Written canonical (LF, sorted, no `#` lines). Read strictly: the whole file is
// validated into a scratch store and `out` is assigned only if it is all good;
// an error is `line N: <reason>`.
// ---------------------------------------------------------------------------

#include <iosfwd>
#include <string>

#include "debug/user_annotations.h"

namespace jnext {
namespace dbg {

/// Read a project file. On success `out` is replaced and `why` untouched; on
/// failure `out` is untouched and `why` is `line N: <reason>`.
bool read_annotations(std::istream& in, UserAnnotations& out, std::string& why);

void write_annotations(std::ostream& out, const UserAnnotations& a);

}  // namespace dbg
}  // namespace jnext
