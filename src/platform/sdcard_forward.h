#pragma once

// ---------------------------------------------------------------------------
// `jnext --sdcard-file-add` forwarded to the jnext that holds the card (GH #568).
//
// A card is held by at most one jnext (core/image_lock.h). When a second jnext is refused the card
// for a `--sdcard-file-add`, the lock file may say that the holder serves ZRCP (the address and the
// BOUND port). This client connects to it and sends one ZRCP command, `sd-file-add`
// (remote/zrcp/zrcp_server.h), which the holder runs on its own cards, so the copy goes through the
// machine that has the card open and a running guest sees the file.
//
// WHAT THE CLIENT TRUSTS: nothing it reads. The lock file is parsed by the one strict parser
// (parse_lock_body); the address is mapped to LOOPBACK of the same family (a wildcard bind reaches
// itself through its loopback) and anything else is refused, so a doctored lock file can send the
// local paths only to another port of this machine; the server's replies are bounded in time (welcome
// 10 s, reply 300 s by default) and size (64 KiB) and only the exact `OK. ` / `Error. [N] ` forms are
// believed, everything else is exit status 5 with a sentence.
// ---------------------------------------------------------------------------

#ifndef __EMSCRIPTEN__

#include <string>

#include "core/image_lock.h"
#include "esp01/esp_socket.h"

namespace sdforward {

/// The address the client connects to for a server bound to `recorded`: `0.0.0.0` and any `127/8`
/// give `127.0.0.1`; `::` and `::1` give `::1`; an IPv4-mapped loopback is normalised first. False,
/// with the sentence to print in `why`, for anything else (a LAN or link-local address, text that is
/// not a numeric IP).
bool connect_address(const std::string& recorded, esp::IpAddress& out, std::string& why);

struct ForwardRequest {
    std::string image;                ///< absolute host path of the card image (never canonicalised)
    std::string host;                 ///< absolute host path of the source
    std::string card_path;            ///< the path the source becomes on the card
    std::string writes;               ///< absolute writes file, when writes_given
    bool        writes_given = false;
    bool        force = false;
    int         slot = -1;            ///< 0 = card 1, 1 = card 2, -1 = any
    bool        writes_lock = false;  ///< the refusal was of the writes file's lock, not the image's
};

/// Ask the jnext described by `holder` (the refusal's LockHolder) to run the copy. Returns the exit
/// status `jnext --sdcard-file-add` must return (0, 2..6; 5 for every reason the running jnext could
/// not be asked or refused). `out_text` is the one-or-more-line text to print: on stdout when the
/// status is 0, else on stderr (it already starts with `error: `).
int forward_file_add(const sdcard::LockHolder& holder, const ForwardRequest& req, std::string& out_text,
                     unsigned welcome_ms = 10000, unsigned reply_ms = 300000);

}  // namespace sdforward

#endif  // !__EMSCRIPTEN__
