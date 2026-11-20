#pragma once

#include <cstdint>

namespace jnext {
namespace dbg {

/// The short display name of NextREG `reg`, "---" for one with no name. Qt-free: the
/// debugger's NextREG panel and the bug report writer both read it.
const char* nextreg_name(uint8_t reg);

}  // namespace dbg
}  // namespace jnext
