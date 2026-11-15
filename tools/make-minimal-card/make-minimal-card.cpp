// make-minimal-card: build the minimal NextZXOS boot card (40 MiB FAT32, 512-byte clusters,
// the boot set plus jnext's two patches) from the distribution you downloaded yourself.
// The same core/minimal_card.cpp builder the browser build runs, so this card IS the card a
// browser makes. A developer tool for local testing, never a way to publish a card: the
// card is copyrighted NextZXOS content, never commit, attach, upload or host it.
#include "core/minimal_card.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void usage(std::FILE* to) {
    std::fprintf(to,
        "Usage: make-minimal-card [--unpinned] [--expect-zip-sha256 HEX] <INPUT> <card.img>\n"
        "  INPUT  the official distribution zip, the cspect-next-1gb.img inside it, or - for\n"
        "         the zip on standard input. The kind is detected from the bytes.\n"
        "  --unpinned            for synthetic test fixtures only: skip every pinned digest\n"
        "                        (zip, image and card) and print the card's SHA-256\n"
        "  --expect-zip-sha256   test tooling: replace the pinned zip SHA-256 (and drop its size\n"
        "                        check) with this one, for a zip made at run time from the image;\n"
        "                        the pinned card SHA-256 still applies\n"
        "The card is copyrighted NextZXOS content: never commit, attach, upload or host it.\n");
}

}  // namespace

int main(int argc, char** argv) {
    bool unpinned = false;
    std::string zip_override;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--unpinned") unpinned = true;
        else if (a == "--expect-zip-sha256") {
            if (i + 1 >= argc) { usage(stderr); return 2; }
            zip_override = argv[++i];
        } else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else if (a.size() > 1 && a[0] == '-') { usage(stderr); return 2; }
        else pos.push_back(a);
    }
    if (pos.size() != 2 || (unpinned && !zip_override.empty())) { usage(stderr); return 2; }
    const std::string& src = pos[0];
    const std::string& dst = pos[1];

    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::symlink_status(dst, ec).type() != fs::file_type::not_found) {
        std::fprintf(stderr, "make-minimal-card: destination exists, not overwritten: %s\n", dst.c_str());
        return 1;
    }

    std::FILE* in = src == "-" ? stdin : std::fopen(src.c_str(), "rb");
    if (!in) { std::fprintf(stderr, "make-minimal-card: cannot open %s\n", src.c_str()); return 1; }
#ifdef _WIN32
    if (in == stdin) _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::vector<uint8_t> buf(1 << 20);
    size_t n = std::fread(buf.data(), 1, buf.size(), in);
    minimal_card::Input kind;
    if (n >= 4 && std::memcmp(buf.data(), "PK\x03\x04", 4) == 0) kind = minimal_card::Input::Zip;
    else if (n >= 512 && buf[510] == 0x55 && buf[511] == 0xAA) kind = minimal_card::Input::Image;
    else { std::fprintf(stderr, "make-minimal-card: not a zip or an SD image: %s\n", src.c_str()); return 1; }
    if (!zip_override.empty() && kind != minimal_card::Input::Zip) {
        std::fprintf(stderr, "make-minimal-card: --expect-zip-sha256 needs the zip as input\n");
        return 2;
    }

    minimal_card::Expect ex = unpinned ? minimal_card::Expect() : minimal_card::pinned(kind);
    if (!zip_override.empty()) { ex.zip_sha256 = zip_override; ex.zip_size = 0; }
    const std::string out = fs::absolute(fs::path(dst), ec).lexically_normal().string();
    minimal_card::Builder b(kind, out, ex);
    while (n > 0) {
        if (b.feed(buf.data(), n) == minimal_card::Status::Error) break;
        n = std::fread(buf.data(), 1, buf.size(), in);
    }
    if (in != stdin) std::fclose(in);
    if (b.error().empty()) b.finish();
    if (!b.error().empty()) {
        std::fprintf(stderr, "make-minimal-card: %s\n", b.error().c_str());
        return 1;
    }
    std::error_code sz_ec;
    std::printf("%s: %llu bytes, sha256 %s\n", out.c_str(),
                static_cast<unsigned long long>(fs::file_size(out, sz_ec)), b.card_sha256().c_str());
    return 0;
}
