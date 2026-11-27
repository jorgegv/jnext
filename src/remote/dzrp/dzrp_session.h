#pragma once

// ---------------------------------------------------------------------------
// The DZRP session token (GH #511, epic #486): how a jnext DZRP server tells that
// the client dialling it is its OWN process's DzrpClient (a session with
// oneself, which would deadlock the emulation thread).
//
// CMD_INIT carries one free-form field, the client's program name, and DZRP
// extensions are not an option. So the client ends its program name with
// ` (session <16 lowercase hex>)`, the token of its process, and the server
// refuses a CMD_INIT whose name ends with ITS OWN process's token. The match is
// exact and on the RAW bytes: no case folding, no other length, the suffix only.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>

namespace jnext {
namespace remote {
namespace dzrp {

/// 16 lowercase hex characters from 64 random bits, made once per process.
const std::string& process_session_token();

/// `jnext v<version> (session <token>)`: the program name a client sends in CMD_INIT.
std::string client_program_name(const std::string& token);

/// True iff the name (raw bytes, ending at the first NUL or at `n`) ENDS with exactly
/// ` (session <token>)`, `token` being 16 lowercase hex characters.
bool name_carries_token(const std::uint8_t* name, std::size_t n, const std::string& token);

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
