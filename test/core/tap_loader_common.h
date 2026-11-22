// Helpers shared by tap_loader_test and tap_loader_autoplay_test (GH #547 stage
// 5: TAPA-01..16 moved out of tap_loader_test so each process is shorter; the
// way halt_batch_corpus.h serves its shards). Each suite is its own process, so
// the counters and the temp root below are per suite.

#pragma once

#include "../row_id.h"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition,
           const std::string& detail = {}) {
    report_row_id(id);
    ++total;
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Bytes = std::vector<uint8_t>;

// One TAP block: [len lo][len hi][flag][payload...][checksum]. `good=false`
// corrupts the checksum while keeping the block structurally complete.
Bytes tap_block(uint8_t flag, const Bytes& payload, bool good = true) {
    uint8_t sum = flag;
    for (uint8_t b : payload) sum ^= b;
    if (!good) sum ^= 0x55;
    const uint16_t len = static_cast<uint16_t>(payload.size() + 2);
    Bytes out = {static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8), flag};
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(sum);
    return out;
}

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

std::filesystem::path g_root;

std::string write_file(const char* name, const Bytes& bytes) {
    const auto path = g_root / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

// A fresh, empty temp root named `<prefix><pid>`; the pid keeps it unique.
void make_root(const char* prefix) {
    g_root = std::filesystem::temp_directory_path() /
             (std::string(prefix) + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);
}

} // namespace
