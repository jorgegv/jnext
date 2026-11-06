// SHA-256 test: sdcard::sha256_hex / sdcard::sha256_file, whichever backend the
// build links (OpenSSL on Linux/macOS, BCrypt on Windows, the portable FIPS 180-4
// one under Emscripten, run by `make wasm-core`).
//
// No VHDL oracle: a host utility. The oracles are NIST FIPS 180-2 Appendix B
// (B.1 "abc", B.2 the 448-bit message, B.3 one million 'a'), the NIST CSRC
// SHA-2 example for the 896-bit message, and GNU coreutils `sha256sum` /
// Python hashlib (independent implementations) for every other digest, computed
// 2026-11-06. The lengths 55/56/63/64/65/119/120/128 sit on both sides of the
// padding boundaries (the length field fits the block or spills into a second
// one); the 256 bytes 0x00..0xFF cover every byte value, signed or not; the two
// files bracket the 1 MiB read chunk. The existing SHA256-01* rows in
// sdcard_provisioner_test stay as they are.
//
// Standalone runner, no GoogleTest, no Qt.

#include "core/sha256.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) { ++g_pass; std::printf("PASS %-10s %s\n", id, desc); }
    else {
        ++g_fail;
        std::printf("FAIL %-10s %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::vector<uint8_t> bytes_of(const std::string& s) { return {s.begin(), s.end()}; }
std::vector<uint8_t> repeat_a(size_t n) { return std::vector<uint8_t>(n, 'a'); }

// "<got> vs <want>" for a failure line; empty when equal.
std::string diff(const std::string& got, const std::string& want) {
    return got == want ? std::string() : got + " vs " + want;
}

void write_file(const std::filesystem::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    using sdcard::sha256_file;
    using sdcard::sha256_hex;

    const std::string ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::string EMPTY = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    const fs::path dir = fs::temp_directory_path() /
        ("jnext_sha256_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);

    check("SHA256-02", "sha256_hex of the empty message",
          sha256_hex({}) == EMPTY, diff(sha256_hex({}), EMPTY));

    {
        const std::string got = sha256_hex(bytes_of("abc"));
        check("SHA256-03", "sha256_hex(\"abc\"), FIPS 180-2 B.1", got == ABC, diff(got, ABC));
    }
    {
        const std::string want = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
        const std::string got = sha256_hex(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
        check("SHA256-04", "sha256_hex of the 448-bit message, FIPS 180-2 B.2", got == want, diff(got, want));
    }
    {
        const std::string want = "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1";
        const std::string got = sha256_hex(bytes_of(
            "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"));
        check("SHA256-05", "sha256_hex of the 896-bit message, NIST CSRC SHA-2 example", got == want, diff(got, want));
    }
    {
        const std::string want = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
        const std::string got = sha256_hex(repeat_a(1000000));
        check("SHA256-06", "sha256_hex of one million 'a', FIPS 180-2 B.3", got == want, diff(got, want));
    }
    {
        const std::string want = "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318";
        const std::string got = sha256_hex(repeat_a(55));
        check("SHA256-07", "sha256_hex of 55 bytes, the last length whose padding fits one block", got == want, diff(got, want));
    }
    {
        const std::string want = "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a";
        const std::string got = sha256_hex(repeat_a(56));
        check("SHA256-08", "sha256_hex of 56 bytes, the padding spills into a second block", got == want, diff(got, want));
    }
    {
        std::string bad;
        bad += diff(sha256_hex(repeat_a(63)), "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
        bad += diff(sha256_hex(repeat_a(64)), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
        bad += diff(sha256_hex(repeat_a(65)), "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
        check("SHA256-09", "sha256_hex of 63, 64 and 65 bytes (around one full block)", bad.empty(), bad);
    }
    {
        std::string bad;
        bad += diff(sha256_hex(repeat_a(119)), "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
        bad += diff(sha256_hex(repeat_a(120)), "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c");
        bad += diff(sha256_hex(repeat_a(128)), "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e");
        check("SHA256-10", "sha256_hex of 119, 120 and 128 bytes (around two full blocks)", bad.empty(), bad);
    }
    {
        std::vector<uint8_t> all(256);
        for (int i = 0; i < 256; ++i) all[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        const std::string want = "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880";
        const std::string got = sha256_hex(all);
        check("SHA256-11", "sha256_hex of the 256 bytes 0x00..0xFF (every byte value, bytes >= 0x80 included)", got == want, diff(got, want));
    }
    {
        write_file(dir / "chunk_plus_one.bin", repeat_a(1048577));
        write_file(dir / "chunk_exact.bin", repeat_a(1048576));
        std::string bad;
        bad += diff(sha256_file((dir / "chunk_plus_one.bin").string()), "4a3f0c0c213adea174f9a3d4c13177315b588bdb2e9c1012d3d0bf0453ca0f6a");
        bad += diff(sha256_file((dir / "chunk_exact.bin").string()), "9bc1b2a288b26af7257a36277ae3816a7d4f16e89c1e7e77d0a5c48bad62b360");
        check("SHA256-12", "sha256_file of 1048577 and 1048576 bytes (either side of the 1 MiB read chunk)", bad.empty(), bad);
    }
    {
        write_file(dir / "abc.bin", bytes_of("abc"));
        write_file(dir / "empty.bin", {});
        std::string bad;
        bad += diff(sha256_file((dir / "abc.bin").string()), ABC);
        bad += diff(sha256_file((dir / "empty.bin").string()), EMPTY);
        check("SHA256-13", "sha256_file of the file \"abc\" and of an empty file", bad.empty(), bad);
    }
    {
        // JNSI-P26b: an unreadable path and a directory both read as "".
        const std::string miss = sha256_file((dir / "no_such_file.bin").string());
        const std::string dd = sha256_file(dir.string());
        check("SHA256-14", "sha256_file of a missing path and of a directory are both empty",
              miss.empty() && dd.empty(), "missing=[" + miss + "] dir=[" + dd + "]");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
