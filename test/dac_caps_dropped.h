// DacCapsDropped: let root honour file permission bits for the scope of one
// object. Shared by sdcard_file_add_test and sdcard_test (single source).
#pragma once

#include <cstring>

// Linux-only mechanism: capabilities do not exist on macOS or Windows. There
// drop() reports false and the one caller that consults it does so only under
// geteuid() == 0, which no CI runner there is.
#ifdef __linux__
#include <linux/capability.h>  // CAP_DAC_*
#include <sys/syscall.h>
#include <unistd.h>            // syscall

// Root reads through permission bits by way of two capabilities. Clearing
// them from the EFFECTIVE set makes root honour mode 0 like anyone else; they
// stay in the permitted set, so the destructor raises them again. Raw
// syscalls, so there is no libcap dependency.
struct DacCapsDropped {
    __user_cap_header_struct hdr{_LINUX_CAPABILITY_VERSION_3, 0};
    __user_cap_data_struct   saved[2]{};
    bool                     active = false;

    bool drop() {
        if (::syscall(SYS_capget, &hdr, saved) != 0) return false;
        __user_cap_data_struct d[2];
        std::memcpy(d, saved, sizeof d);
        for (int cap : {CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH})
            d[cap / 32].effective &= ~(1u << (cap % 32));
        if (::syscall(SYS_capset, &hdr, d) != 0) return false;
        active = true;
        return true;
    }
    ~DacCapsDropped() {
        if (active) ::syscall(SYS_capset, &hdr, saved);
    }
};
#elif defined(__EMSCRIPTEN__)
#include <cstdio>

// A WebAssembly module cannot change its host process's capabilities, so drop()
// drops nothing: it reports whether the RUNNER did (test/node-run.sh starts node
// without them when it is root). Under NODERAWFS /proc/self/status is the host's,
// and `self` is node. True iff neither CAP_DAC_OVERRIDE (1) nor
// CAP_DAC_READ_SEARCH (2) is in the effective set; otherwise the caller's own
// refusal arm SKIPs, loudly.
struct DacCapsDropped {
    bool drop() {
        std::FILE* f = std::fopen("/proc/self/status", "r");
        if (!f) return false;
        char line[256];
        bool found = false;
        unsigned long long eff = 0;
        while (std::fgets(line, sizeof line, f)) {
            if (std::sscanf(line, "CapEff: %llx", &eff) == 1) {
                found = true;
                break;
            }
        }
        std::fclose(f);
        return found && (eff & ((1ull << 1) | (1ull << 2))) == 0;
    }
};
#else
struct DacCapsDropped {
    bool drop() { return false; }
};
#endif
