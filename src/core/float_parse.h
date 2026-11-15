#pragma once
// Locale-independent text -> double, whole string or nothing.
//
// std::from_chars for floating point is missing from libc++ before LLVM 20
// (Xcode 16.4 on macos-15: `call to deleted function 'from_chars'`), and
// strtod/stod/stof follow LC_NUMERIC, which a Qt app sets from the host
// (setlocale(LC_ALL, "")): a comma-decimal host would read "1.5" as 1. So:
//   - parse_double_chars: std::from_chars, compiled only where the library
//     defines __cpp_lib_to_chars. libstdc++ does (Linux, MinGW); libc++ never
//     does, whatever its version, so every Apple build, arm64 included, takes the
//     stream arm;
//   - parse_double_stream: an istringstream imbued with the classic locale,
//     compiled everywhere, so both arms are testable on one host;
//   - parse_double: the first when it exists, else the second.
// Grammar, the same in every arm: an optional '-', decimal digits with an
// optional '.', an optional exponent. Hex floats, inf, nan, a '+', leading or
// trailing blanks and any trailing junk are refused; so are values that overflow,
// and any nonzero result that is not a normal double (underflow to zero, and
// subnormals: libstdc++ returns those, libc++'s stream refuses them, so every
// platform is made to refuse them).
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <version>

namespace float_parse {

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define JNEXT_HAVE_FLOAT_FROM_CHARS 1
inline bool parse_double_chars(const char* b, const char* e, double& out) {
    if (b >= e) return false;
    double d = 0;
    const std::from_chars_result r = std::from_chars(b, e, d);
    if (r.ec != std::errc() || r.ptr != e || !std::isfinite(d)) return false;   // from_chars reads inf/nan
    if (d != 0 && !std::isnormal(d)) return false;                              // subnormal
    out = d;
    return true;
}
#endif

inline bool parse_double_stream(const char* b, const char* e, double& out) {
    if (b >= e) return false;
    // The grammar is checked by hand first: a stream accepts a leading '+', blanks and
    // "0x" prefixes differently per library.
    const char* p = b;
    if (*p == '-') ++p;
    bool digits = false, nonzero = false;
    for (; p < e && *p >= '0' && *p <= '9'; ++p) { digits = true; nonzero |= (*p != '0'); }
    if (p < e && *p == '.') {
        for (++p; p < e && *p >= '0' && *p <= '9'; ++p) { digits = true; nonzero |= (*p != '0'); }
    }
    if (!digits) return false;
    if (p < e && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < e && (*p == '-' || *p == '+')) ++p;
        const char* x = p;
        while (p < e && *p >= '0' && *p <= '9') ++p;
        if (p == x) return false;
    }
    if (p != e) return false;
    std::istringstream is{std::string(b, e)};
    is.imbue(std::locale::classic());
    double d = 0;
    is >> std::noskipws >> d;
    if (is.fail() || (nonzero && d == 0)) return false;   // overflow, or underflow to 0
    if (d != 0 && !std::isnormal(d)) return false;        // subnormal
    out = d;
    return true;
}

inline bool parse_double(const char* b, const char* e, double& out) {
#ifdef JNEXT_HAVE_FLOAT_FROM_CHARS
    return parse_double_chars(b, e, out);
#else
    return parse_double_stream(b, e, out);
#endif
}

}  // namespace float_parse
