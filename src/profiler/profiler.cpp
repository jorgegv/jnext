#include "profiler/profiler.h"

#include "core/log.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

Profiler::Profiler() = default;

Profiler::~Profiler() {
    shutdown();
}

bool Profiler::init() {
    if (entries_) return true;   // idempotent
    bytes_alloc_ = static_cast<size_t>(kNumEntries) * sizeof(Entry);
    void* p = ::mmap(nullptr, bytes_alloc_,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    if (p == MAP_FAILED) {
        Log::emulator()->error("profiler: mmap({} bytes) failed: {}",
                               bytes_alloc_, std::strerror(errno));
        entries_     = nullptr;
        bytes_alloc_ = 0;
        return false;
    }
    entries_ = static_cast<Entry*>(p);
    // MAP_ANONYMOUS pages are zero-filled by the kernel; no memset needed.
    Log::emulator()->info("profiler: allocated {} MB (mmap MAP_ANONYMOUS)",
                          bytes_alloc_ / (1024 * 1024));
    return true;
}

void Profiler::shutdown() {
    if (!entries_) return;
    ::munmap(entries_, bytes_alloc_);
    entries_     = nullptr;
    bytes_alloc_ = 0;
}

bool Profiler::write_to_file(const std::string& path) const {
    if (!entries_) {
        Log::emulator()->warn("profiler: write_to_file('{}') with no data — "
                              "profiler was never initialised", path);
        return false;
    }
    std::FILE* fp = std::fopen(path.c_str(), "w");
    if (!fp) {
        Log::emulator()->error("profiler: fopen('{}') failed: {}",
                               path, std::strerror(errno));
        return false;
    }
    // Iterate in ascending physical-key order — the array is already
    // laid out that way.
    size_t emitted = 0;
    for (uint32_t key = 0; key < kNumEntries; ++key) {
        const Entry& e = entries_[key];
        if (e.tstates == 0) continue;
        const uint8_t  bank = static_cast<uint8_t>(key >> 13);
        const uint16_t log  = e.last_logical_pc;
        // Output column 1: bank byte + 16-bit logical PC (6 hex digits).
        // Output column 2: logical PC alone (4 hex digits) — convenient
        //   for the v1 Perl join path that ignores the bank.
        // Output column 3: accumulated T-states.
        std::fprintf(fp, "%02x%04x %04x %llu\n",
                     bank, log, log,
                     static_cast<unsigned long long>(e.tstates));
        ++emitted;
    }
    std::fclose(fp);
    Log::emulator()->info("profiler: wrote {} non-zero entries to {}",
                          emitted, path);
    return true;
}
