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
        const uint16_t log  = e.last_logical_pc;
        // Output column 1: real 21-bit physical address (6 hex digits),
        //   i.e. `(page << 13) | (pc & 0x1FFF)` — the same value as
        //   the array index `key`. Range 0..0x1FFFFF for the 2 MB Next
        //   SRAM. E.g. bank 5, intra-page offset $0 → `00a000`; bank
        //   5, intra-page offset $1FFF → `00bfff`. This is stable per
        //   physical byte regardless of which slot mapped it.
        // Output column 2: last observed 16-bit logical PC (4 hex
        //   digits). Identifies the slot the CPU was executing from.
        //   Last-wins on aliasing (same physical byte hit from two
        //   slots) — see the caveat in profiler.h.
        // Output column 3: accumulated T-states (decimal, uint64).
        std::fprintf(fp, "%06x %04x %llu\n",
                     key, log,
                     static_cast<unsigned long long>(e.tstates));
        ++emitted;
    }
    std::fclose(fp);
    Log::emulator()->info("profiler: wrote {} non-zero entries to {}",
                          emitted, path);
    return true;
}
