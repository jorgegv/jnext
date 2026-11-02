#include "debug/reexport.h"

namespace jnext {
namespace dbg {
namespace reexport {

bool name_exportable(const std::string& name) {
    if (name.empty()) return false;
    for (unsigned char c : name)
        if (c <= ' ' || c == 0x7F) return false;
    return true;
}

}  // namespace reexport
}  // namespace dbg
}  // namespace jnext
