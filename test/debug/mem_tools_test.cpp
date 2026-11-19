// Debugger Memory panel find / fill / block-split helper (GH #370): jnext::dbg::memtools.
//
// Oracle: the issue's stated behaviour and the backend's documented poke/peek contract
// (debugger_inspect.cpp: a Cpu poke is one verb, a Page poke stops at its 8K page, pages 00-DF
// have backing store, a ROM image is 16K). Host UI policy: no VHDL oracle. The expected values
// below are derived by hand from the grammar in mem_tools.h, never from the code.
//
// Run: ./build/test/mem_tools_test

#include "debug/mem_tools.h"

#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

using namespace jnext::dbg;
using namespace jnext::dbg::memtools;

static int g_total = 0, g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char* id, const char* desc, bool ok, const std::string& detail = "") {
    report_row_id(id);
    ++g_total;
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  FAIL %s: %s %s\n", id, desc, detail.c_str()); }
}

using V = std::vector<uint8_t>;

static bool is(const std::optional<Pattern>& p, const V& b, const V& m) {
    return p && p->bytes == b && p->mask == m;
}
static std::string why_of_hex(const char* t, const char* m = "") {
    std::string w;
    return parse_hex_pattern(t, m, &w) ? "<accepted>" : w;
}

int main() {
    // MEMT-01
    {
        const V b{0xCD, 0x34, 0x12}, m{0xFF, 0xFF, 0xFF};
        check("MEMT-01", "hex pattern spellings: spaced, packed, lower case",
              is(parse_hex_pattern("CD 34 12", "", nullptr), b, m) &&
              is(parse_hex_pattern("CD3412", "", nullptr), b, m) &&
              is(parse_hex_pattern("cd3412", "", nullptr), b, m));
    }
    // MEMT-02
    check("MEMT-02", "nibble wildcards: C? ?? 1? -> bytes C0 00 10, mask F0 00 F0",
          is(parse_hex_pattern("C? ?? 1?", "", nullptr), {0xC0, 0x00, 0x10}, {0xF0, 0x00, 0xF0}));
    // MEMT-03
    check("MEMT-03", "an explicit mask is ANDed with the wildcard mask",
          is(parse_hex_pattern("21 00 40", "FF 00 FF", nullptr), {0x21, 0x00, 0x40}, {0xFF, 0x00, 0xFF}) &&
          is(parse_hex_pattern("2? 00", "FF 0F", nullptr), {0x20, 0x00}, {0xF0, 0x0F}));
    // MEMT-04
    {
        int ok = 0, n = 0;
        auto refuse = [&](const char* t, const char* m, const char* want) {
            ++n;
            const std::string w = why_of_hex(t, m);
            if (w == want) ++ok; else std::printf("    MEMT-04 '%s': got '%s'\n", t, w.c_str());
        };
        refuse("", "", "no bytes given");
        refuse("CD3", "", "odd number of hex digits in 'CD3'");
        refuse("CG", "", "not a hex byte: 'CG'");
        refuse("$CD", "", "not a hex byte: '$CD'");
        refuse("0xCD", "", "not a hex byte: '0xCD'");
        refuse("CD 34", "FF", "the mask has 1 bytes and the pattern 2");
        refuse("CD", "F?", "not a hex byte: 'F?'");
        refuse("?? ??", "", "a pattern of only wildcards matches everywhere");
        check("MEMT-04", "hex pattern refusals carry the documented text (8 cases)", n == 8 && ok == 8);
    }
    // MEMT-05
    {
        std::string w1, w2;
        const auto c = text_pattern("Hi!", true, nullptr);
        const auto i = text_pattern("Hi!", false, nullptr);
        const bool e = !text_pattern("", true, &w1) && !text_pattern("caf\xE9", false, &w2);
        check("MEMT-05", "text patterns: case on, case off, refusals",
              is(c, {0x48, 0x69, 0x21}, {0xFF, 0xFF, 0xFF}) && is(i, {0x48, 0x49, 0x21}, {0xDF, 0xDF, 0xFF}) &&
              e && w1 == "no text given" && w2 == "only printable ASCII text can be searched for",
              w1 + " / " + w2);
    }
    // MEMT-06
    {
        V d(32, 0);
        d[5] = 0xAA; d[6] = 0xBB; d[20] = 0xAA; d[21] = 0xBB;
        const Pattern p{{0xAA, 0xBB}, {0xFF, 0xFF}};
        auto h0 = find(d.data(), d.size(), p, 0), h5 = find(d.data(), d.size(), p, 5);
        auto h6 = find(d.data(), d.size(), p, 6), h21 = find(d.data(), d.size(), p, 21);
        check("MEMT-06", "find: first hit from, inclusive of from, then wrap",
              h0 && h0->offset == 5 && !h0->wrapped && h5 && h5->offset == 5 && !h5->wrapped &&
              h6 && h6->offset == 20 && !h6->wrapped && h21 && h21->offset == 5 && h21->wrapped);
    }
    // MEMT-07
    {
        V d(12, 0);
        d[0] = 0xCD; d[1] = 0x00; d[2] = 0x81;
        d[3] = 0xCD; d[4] = 0x00; d[5] = 0x80;
        d[6] = 0xCD; d[7] = 0xFF; d[8] = 0x80;
        const Pattern p{{0xCD, 0x00, 0x80}, {0xFF, 0x00, 0xFF}};
        auto a = find(d.data(), d.size(), p, 0), b = find(d.data(), d.size(), p, 4);
        check("MEMT-07", "find honours the mask: CD ?? 80 skips CD 00 81, hits 3 then 6",
              a && a->offset == 3 && b && b->offset == 6);
    }
    // MEMT-08
    {
        // The buffer holds 17 bytes but the search is given 16: byte 16 is the third byte of the pattern,
        // so a match that ran past `size` would find it.
        V d(17, 0);
        d[14] = 0x11; d[15] = 0x22; d[16] = 0x33;  // the first two bytes of a 3-byte pattern at the end
        const Pattern p{{0x11, 0x22, 0x33}, {0xFF, 0xFF, 0xFF}};
        const bool none = !find(d.data(), 16, p, 0);
        d[13] = 0x11; d[14] = 0x22; d[15] = 0x33;
        auto at13 = find(d.data(), 16, p, 0);
        auto w = find(d.data(), 16, p, 16);
        const Pattern big{V(17, 0), V(17, 0xFF)};
        check("MEMT-08", "find never matches past the end; from >= size wraps; a longer pattern is nullopt",
              none && at13 && at13->offset == 13 && w && w->offset == 13 && w->wrapped &&
              !find(d.data(), 16, big, 0));
    }
    // MEMT-09
    {
        const auto key = text_pattern("key", false, nullptr);
        const V upper{'K', 'E', 'Y'}, mixed{'k', 'E', 'y'}, at{'A', 0x60};
        const auto at_p = text_pattern("a@", false, nullptr);
        check("MEMT-09", "case-off text matches either case; a non-letter keeps mask FF",
              find(upper.data(), 3, *key, 0) && find(mixed.data(), 3, *key, 0) &&
              !find(at.data(), 2, *at_p, 0));
    }
    // MEMT-10
    {
        std::string w1, w2;
        const auto f = parse_fill("DE AD BE", nullptr);
        const auto z = parse_fill("00", nullptr);
        check("MEMT-10", "fill value parse, refusals and the cyclic buffer",
              f && *f == V({0xDE, 0xAD, 0xBE}) && z && *z == V({0x00}) &&
              !parse_fill("??", &w1) && !parse_fill("", &w2) &&
              fill_buffer({0xDE, 0xAD, 0xBE}, 8) == V({0xDE, 0xAD, 0xBE, 0xDE, 0xAD, 0xBE, 0xDE, 0xAD}) &&
              fill_buffer({0xDE, 0xAD, 0xBE}, 2) == V({0xDE, 0xAD}));
    }
    // MEMT-11
    {
        std::string w;
        const auto a = parse_length("1B00", nullptr), b = parse_length("$1B00", nullptr),
                   c = parse_length("0x1B00", nullptr), z = parse_length("0", nullptr);
        const bool bad = !parse_length("xyz", &w);
        check("MEMT-11", "hex length spellings and refusal",
              a && *a == 0x1B00 && b && *b == 0x1B00 && c && *c == 0x1B00 && z && *z == 0 && bad &&
              w == "not a hex length: 'xyz'", w);
    }
    // MEMT-15
    {
        std::string w1, w2;
        const auto ok8 = parse_length("FFFFFFFF", nullptr);
        const bool r = !parse_length("123456789", &w1) && !parse_length("$123456789", &w2);
        check("MEMT-15", "hex length: 8 digits are the most (FFFFFFFF parses; 9 digits are refused with the exact text)",
              ok8 && *ok8 == 0xFFFFFFFFu && r && w1 == "not a hex length: '123456789'" &&
              w2 == "not a hex length: '$123456789'", w1 + " / " + w2);
    }
    // MEMT-12
    {
        std::string w1, w2, w3;
        const auto a = split_block(MemSpace::cpu(), 0x4000, 0x1B00, nullptr);
        const auto b = split_block(MemSpace::cpu(), 0xF000, 0x1000, nullptr);
        const bool r1 = !split_block(MemSpace::cpu(), 0xF000, 0x1001, &w1);
        const bool r2 = !split_block(MemSpace::cpu(), 0x10000, 1, &w2);
        const bool r3 = !split_block(MemSpace::cpu(), 0x4000, 0, &w3);
        check("MEMT-12", "Cpu blocks: one chunk, never wrap, refusal texts, block_room",
              a && a->size() == 1 && (*a)[0].addr == 0x4000 && (*a)[0].len == 0x1B00 &&
              b && b->size() == 1 && r1 && w1 == "the block runs past $FFFF" &&
              r2 && w2 == "the start is past $FFFF" && r3 && w3 == "the length is 0" &&
              block_room(MemSpace::cpu(), 0xF000) == 0x1000,
              w1 + " / " + w2 + " / " + w3);
    }
    // MEMT-13
    {
        std::string w1, w2, w3;
        const auto a = split_block(MemSpace::page(0x0A), 0x1000, 0x3000, nullptr);
        const auto b = split_block(MemSpace::page(0xDF), 0, 0x2000, nullptr);
        const bool r1 = !split_block(MemSpace::page(0xDF), 0, 0x2001, &w1);
        const bool r2 = !split_block(MemSpace::page(0x00), 0x2000, 1, &w2);
        const bool r3 = !split_block(MemSpace::page(0xE0), 0, 1, &w3);
        const bool shape = a && a->size() == 2 && (*a)[0].space == MemSpace::page(0x0A) &&
                           (*a)[0].addr == 0x1000 && (*a)[0].len == 0x1000 &&
                           (*a)[1].space == MemSpace::page(0x0B) && (*a)[1].addr == 0 && (*a)[1].len == 0x2000;
        check("MEMT-13", "Page blocks run on page by page up to DF; refusal texts; block_room",
              shape && b && b->size() == 1 && r1 && w1 == "the block runs past page DF" &&
              r2 && w2 == "the start is outside the 8K page" && r3 && w3 == "the slot is paged to no memory" &&
              block_room(MemSpace::page(0x10), 0x100) == 0xD0u * 0x2000u - 0x100,
              w1 + " / " + w2 + " / " + w3);
    }
    // MEMT-14
    {
        std::string w1, w2, w3;
        const auto a = split_block(MemSpace::rom(1), 0x2000, 0x2000, nullptr);
        const bool r1 = !split_block(MemSpace::rom(1), 0x2000, 0x2001, &w1);
        const bool r2 = !split_block(MemSpace::rom(1), 0x4000, 1, &w2);
        const bool r3 = !split_block(MemSpace::divmmc_ram(0), 0, 1, &w3);
        check("MEMT-14", "Rom blocks stay in the 16K image; other kinds are refused; block_room",
              a && a->size() == 1 && r1 && w1 == "the block runs past the end of ROM1" &&
              r2 && w2 == "the start is outside the 16K ROM" &&
              r3 && w3 == "this memory cannot be addressed as a block" &&
              block_room(MemSpace::rom(0), 0x3000) == 0x1000,
              w1 + " / " + w2 + " / " + w3);
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
