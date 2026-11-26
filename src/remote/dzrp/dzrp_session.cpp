#include "remote/dzrp/dzrp_session.h"

#include <chrono>
#include <cstdio>
#include <random>

#include "version.h"

namespace jnext {
namespace remote {
namespace dzrp {

static bool is_token(const std::string& t) {
    if (t.size() != 16) return false;
    for (char c : t)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

static std::string make_token() {
    // random_device can be weak (old MinGW): mix in the clock and an address.
    static int anchor = 0;
    std::random_device rd;
    std::uint64_t v = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    v ^= static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    v ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&anchor)) * 0x9E3779B97F4A7C15ull;
    v ^= static_cast<std::uint64_t>(
             std::chrono::system_clock::now().time_since_epoch().count()) << 17;
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

const std::string& process_session_token() {
    static const std::string token = make_token();
    return token;
}

std::string client_program_name(const std::string& token) {
    return std::string("jnext v") + JNEXT_VERSION_STRING + " (session " + token + ")";
}

bool name_carries_token(const std::uint8_t* name, std::size_t n, const std::string& token) {
    if (!is_token(token)) return false;
    std::size_t len = 0;
    while (len < n && name[len] != 0) ++len;
    const std::string suffix = " (session " + token + ")";
    if (len < suffix.size()) return false;
    return std::string(reinterpret_cast<const char*>(name) + (len - suffix.size()),
                       suffix.size()) == suffix;
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
