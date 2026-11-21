#pragma once

/// The host platform's name as the `.jns` manifest's `producer.platform` and the bug
/// report's `producer.platform` write it: one function, so the two files cannot name
/// the same machine differently.
inline const char* host_platform_name() {
#ifdef __linux__
    return "linux";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}
