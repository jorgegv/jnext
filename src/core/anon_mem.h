#pragma once

#include <cstddef>

// Portable large anonymous allocation, zero-filled and lazily backed by the OS
// (pages fault in on first touch, so reserving a big buffer costs no RSS until
// it is actually written). POSIX uses mmap(MAP_ANONYMOUS); Windows uses
// VirtualAlloc(MEM_COMMIT), whose pages are likewise demand-zero. This keeps
// the mmap dependency out of the rest of the codebase so subsystems that need
// such a buffer (RewindBuffer, Profiler) build unchanged when cross-compiling
// for Windows (MinGW), which has no <sys/mman.h>.
//
// Header-only (inline) on purpose: the two callers live in sibling static libs
// (jnext_debug, jnext_profiler) that jnext_core already depends on, so a .cpp
// in jnext_core would form a circular static-link dependency. Inlining sidesteps
// it entirely — each TU compiles its own copy, ODR-safe via inline linkage.

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

namespace anon_mem {

// Reserve+commit `bytes` of zero-filled memory. Returns nullptr on failure.
inline void* alloc(std::size_t bytes)
{
#if defined(_WIN32)
    // MEM_COMMIT pages are demand-zero (backed by physical pages lazily on
    // first access), mirroring mmap(MAP_ANONYMOUS).
    return ::VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
#endif
}

// Release a block returned by alloc(). `bytes` must be the size passed to
// alloc() (POSIX munmap needs it; the Windows path ignores it).
inline void free(void* p, std::size_t bytes)
{
    if (!p) return;
#if defined(_WIN32)
    (void)bytes;                      // VirtualFree(MEM_RELEASE) needs size 0
    ::VirtualFree(p, 0, MEM_RELEASE);
#else
    ::munmap(p, bytes);
#endif
}

} // namespace anon_mem
