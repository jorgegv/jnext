// The SD card's write journal (--sdcard-writes): the guest's sector writes go
// to a side file and are laid over the image on every read, so the image is
// never written.
//
// Oracles:
//   - SD Physical Layer Simplified Spec: CMD17/CMD18/CMD24 data and response
//     tokens (4.7.4, 7.3.3.1), CMD16 block length on a byte-addressed card.
//   - The journal file format table in the developer guide (media chapter,
//     "The SD card's write journal"): the expected FILE bytes are built here
//     from that table, never through the journal's own writer.
//   - The binding rules: a journal belongs to one image (its SHA-256, else
//     its size and mtime); another image's journal is refused, and a file
//     that is not a journal is never touched.
//
// The fixture is a 64 KiB card whose byte i of sector s is (s*7 + i) & 255.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "peripheral/sd_card.h"
#include "peripheral/sd_write_journal.h"
#include "../row_id.h"
#include "../test_portable.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace swj = sd_write_journal;

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

constexpr uint32_t kSectors = 128;   // 64 KiB

uint8_t blob_byte(uint32_t sector, int i) {
    return static_cast<uint8_t>((sector * 7 + i) & 0xFF);
}

using Bytes = std::vector<uint8_t>;

Bytes sector_bytes(uint32_t sector) {
    Bytes b(512);
    for (int i = 0; i < 512; ++i) b[i] = blob_byte(sector, i);
    return b;
}

Bytes payload(uint8_t salt) {
    Bytes b(512);
    for (int i = 0; i < 512; ++i) b[i] = static_cast<uint8_t>((i * 13 + salt) & 0xFF);
    return b;
}

std::string g_dir;

std::string make_image(const std::string& name) {
    const std::string p = g_dir + "/" + name;
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    for (uint32_t s = 0; s < kSectors; ++s) {
        const Bytes b = sector_bytes(s);
        o.write(reinterpret_cast<const char*>(b.data()), 512);
    }
    return p;
}

Bytes slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

void put_le(Bytes& v, uint64_t x, int n) {
    for (int i = 0; i < n; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

swj::Binding digest_binding(uint8_t fill) {
    swj::Binding b;
    b.bytes = kSectors * 512ull;
    b.sha256.fill(fill);
    return b;
}

swj::Binding desktop_binding(int64_t mtime) {
    swj::Binding b;
    b.bytes = kSectors * 512ull;
    b.mtime = mtime;
    return b;
}

// The journal file per the format table, built from the table alone.
Bytes expected_header(const swj::Binding& b) {
    Bytes h = {'J', 'N', 'E', 'X', 'T', 'S', 'D', 'W'};
    put_le(h, 1, 4);
    put_le(h, 512, 4);
    put_le(h, b.bytes, 8);
    put_le(h, static_cast<uint64_t>(b.mtime), 8);
    h.insert(h.end(), b.sha256.begin(), b.sha256.end());
    return h;
}

void add_record(Bytes& f, uint32_t sector, const Bytes& data) {
    put_le(f, sector, 4);
    f.insert(f.end(), data.begin(), data.end());
}

// A kept-file record per the format table: 0xFFFFFFFF, L u32 (the bytes after it),
// n u16, the name, the bytes.
void add_kept(Bytes& f, const std::string& name, const Bytes& data) {
    put_le(f, 0xFFFFFFFFull, 4);
    put_le(f, 2 + name.size() + data.size(), 4);
    put_le(f, name.size(), 2);
    f.insert(f.end(), name.begin(), name.end());
    f.insert(f.end(), data.begin(), data.end());
}

// SPI helpers
void spi_write(SdCardDevice& sd, uint8_t v) { (void)sd.receive(v); }
uint8_t spi_read(SdCardDevice& sd) { return sd.send(); }

uint8_t send_cmd_r1(SdCardDevice& sd, uint8_t cmd, uint32_t arg) {
    spi_write(sd, 0x40 | (cmd & 0x3F));
    spi_write(sd, (arg >> 24) & 0xFF);
    spi_write(sd, (arg >> 16) & 0xFF);
    spi_write(sd, (arg >> 8) & 0xFF);
    spi_write(sd, arg & 0xFF);
    spi_write(sd, 0x95);
    for (int i = 0; i < 16; ++i) {
        const uint8_t b = spi_read(sd);
        if (b != 0xFF) return b;
    }
    return 0xFF;
}

void init_card(SdCardDevice& sd, bool sdhc) {
    (void)send_cmd_r1(sd, 0, 0);
    (void)send_cmd_r1(sd, 8, 0x1AA);
    (void)send_cmd_r1(sd, 55, 0);
    (void)send_cmd_r1(sd, 41, sdhc ? 0x40000000 : 0);
    (void)send_cmd_r1(sd, 58, 0);
}

bool wait_token(SdCardDevice& sd) {
    for (int i = 0; i < 64; ++i)
        if (spi_read(sd) == 0xFE) return true;
    return false;
}

// CMD24 of `arg` (a block number on SDHC, a byte address on SDSC); returns the
// data response token, or the R1 when the command itself was refused.
uint8_t write_block(SdCardDevice& sd, uint32_t arg, const Bytes& data) {
    const uint8_t r1 = send_cmd_r1(sd, 24, arg);
    if (r1 != 0x00) { sd.deselect(); return r1; }
    spi_write(sd, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(sd, data[i]);
    spi_write(sd, 0xFF);
    spi_write(sd, 0xFF);
    uint8_t resp = 0xFF;
    for (int i = 0; i < 32; ++i) {
        const uint8_t b = spi_read(sd);
        if (b != 0xFF) { resp = b; break; }
    }
    sd.deselect();
    return resp;
}

// CMD17 of `arg`, `len` data bytes.
bool read_block(SdCardDevice& sd, uint32_t arg, Bytes& out, size_t len = 512) {
    if (send_cmd_r1(sd, 17, arg) != 0x00) return false;
    if (!wait_token(sd)) return false;
    out.resize(len);
    for (size_t i = 0; i < len; ++i) out[i] = spi_read(sd);
    (void)spi_read(sd);
    (void)spi_read(sd);
    sd.deselect();
    return true;
}

bool reads(SdCardDevice& sd, uint32_t sector, const Bytes& want) {
    Bytes got;
    return read_block(sd, sector, got) && got == want;
}

std::string hex_of(const std::array<uint8_t, 32>& d) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (uint8_t c : d) { s += x[c >> 4]; s += x[c & 15]; }
    return s;
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A mounted card on `img` in SDHC mode.
struct Card {
    SdCardDevice sd;
    explicit Card(const std::string& img, bool sdhc = true) {
        sd.mount(img, false);
        init_card(sd, sdhc);
    }
};

int g_hook_count = 0;
void count_hook() { ++g_hook_count; }

}  // namespace

int main() {
    std::printf("SD card write journal rows\n");
    g_dir = jtp::make_temp_dir("jnext-sdw-");
    if (g_dir.empty()) { std::printf("cannot make a scratch directory\n"); return 1; }
    int n_dir = 0;
    auto scratch = [&]() {
        const std::string d = g_dir + "/c" + std::to_string(++n_dir);
        fs::create_directories(d);
        return d;
    };

    // SDW-01 / SDW-02: one write, journal mode.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const Bytes before = slurp(img);
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        const bool bound = swj::bind(img, jf, b, false, why);
        const Bytes w = payload(1);
        uint8_t tok = 0;
        bool rb = false;
        std::uint64_t attempts = 99;
        {
            Card c(img);
            tok = write_block(c.sd, 5, w);
            rb = reads(c.sd, 5, w);
            attempts = c.sd.stream_write_attempts_for_test();
        }
        check("SDW-01",
              "journal mode: CMD24 is accepted (0x05) and read back by CMD17, the image "
              "file is byte-identical and no write reached its stream (SD spec 7.3.3.1)",
              bound && tok == 0x05 && rb && slurp(img) == before && attempts == 0,
              "bound=" + std::to_string(bound) + " tok=" + std::to_string(tok) +
              " rb=" + std::to_string(rb) + " attempts=" + std::to_string(attempts) + " " + why);
        Bytes want = expected_header(b);
        add_record(want, 5, w);
        check("SDW-02",
              "after one write the journal file is exactly the 64-byte header and one "
              "516-byte record, as the format table lays them out",
              slurp(jf) == want, "size=" + std::to_string(slurp(jf).size()));
        swj::forget_all_for_test();
    }

    // SDW-03 / SDW-04: a reload (registry forgotten, journal rebound from its file).
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        swj::bind(img, jf, b, false, why);
        const Bytes w = payload(2);
        {
            Card c(img);
            (void)write_block(c.sd, 20, w);
        }
        swj::forget_all_for_test();
        const bool rebound = swj::bind(img, jf, b, false, why);
        {
            Card c(img);
            check("SDW-03",
                  "a new device on the rebound journal (a reload) reads the written sector "
                  "from the journal and its neighbours from the image",
                  rebound && reads(c.sd, 20, w) && reads(c.sd, 19, sector_bytes(19)) &&
                  reads(c.sd, 21, sector_bytes(21)), why);
            // CMD18 across 19, 20, 21.
            bool multi = send_cmd_r1(c.sd, 18, 19) == 0x00;
            const Bytes want[3] = {sector_bytes(19), w, sector_bytes(21)};
            for (int k = 0; multi && k < 3; ++k) {
                multi = wait_token(c.sd);
                for (int i = 0; multi && i < 512; ++i) multi = spi_read(c.sd) == want[k][i];
                (void)spi_read(c.sd);
                (void)spi_read(c.sd);
            }
            (void)send_cmd_r1(c.sd, 12, 0);
            c.sd.deselect();
            check("SDW-04",
                  "CMD18 across three sectors with only the middle one journaled returns "
                  "image, journal, image",
                  multi);
        }
        swj::forget_all_for_test();
    }

    // SDW-05: a byte-addressed card with a 16-byte block length.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        std::string why;
        swj::bind(img, d + "/w.jsdw", digest_binding(0xAB), false, why);
        const Bytes w = payload(3);
        Card c(img, false);
        const uint8_t tok = write_block(c.sd, 7 * 512, w);
        const uint8_t r16 = send_cmd_r1(c.sd, 16, 16);
        Bytes slice, other;
        const bool a = read_block(c.sd, 7 * 512 + 8, slice, 16);
        const bool o = read_block(c.sd, 8 * 512 + 4, other, 16);
        const Bytes ws(w.begin() + 8, w.begin() + 24);
        const Bytes s8 = sector_bytes(8);
        const Bytes os(s8.begin() + 4, s8.begin() + 20);
        check("SDW-05",
              "SDSC card, CMD16 block length 16: a read inside a journaled sector returns "
              "that slice of the written bytes, and one inside another sector the image's",
              tok == 0x05 && r16 == 0x00 && a && o && slice == ws && other == os,
              "tok=" + std::to_string(tok) + " r16=" + std::to_string(r16));
        c.sd.unmount();
        swj::forget_all_for_test();
    }

    // SDW-06: the same sector twice; a remount compacts.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        swj::bind(img, jf, b, false, why);
        const Bytes a = payload(4), bb = payload(5);
        bool second_wins = false;
        {
            Card c(img);
            (void)write_block(c.sd, 30, a);
            (void)write_block(c.sd, 30, bb);
            second_wins = reads(c.sd, 30, bb);
        }
        const size_t before = slurp(jf).size();
        swj::forget_all_for_test();
        swj::bind(img, jf, b, false, why);
        const size_t after = slurp(jf).size();
        bool after_ok = false;
        {
            Card c(img);
            after_ok = reads(c.sd, 30, bb);
        }
        check("SDW-06",
              "a sector written twice: the second copy wins on read-back and after a "
              "remount, and the remount compacts the file to one record",
              second_wins && after_ok && before == 64 + 2 * 516u && after == 64 + 516u,
              "before=" + std::to_string(before) + " after=" + std::to_string(after));
        swj::forget_all_for_test();
    }

    // SDW-07 / SDW-08 / SDW-10: refusals.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        std::string why;
        swj::bind(img, jf, digest_binding(0xAB), false, why);
        {
            Card c(img);
            (void)write_block(c.sd, 2, payload(6));
        }
        swj::forget_all_for_test();
        const Bytes bytes = slurp(jf);
        const swj::Binding other = digest_binding(0xCD);
        const bool ok = swj::bind(img, jf, other, false, why);
        check("SDW-07",
              "a journal bound to another digest is refused at bind; the reason names both "
              "digests and --sdcard-writes-reset, and the file is untouched",
              !ok && has(why, hex_of(digest_binding(0xAB).sha256)) &&
              has(why, hex_of(other.sha256)) && has(why, "--sdcard-writes-reset") &&
              slurp(jf) == bytes, why);
        swj::forget_all_for_test();

        const std::string jd = d + "/d.jsdw";
        swj::bind(img, jd, desktop_binding(100), false, why);
        {
            Card c(img);
            (void)write_block(c.sd, 2, payload(7));
        }
        swj::forget_all_for_test();
        const Bytes dbytes = slurp(jd);
        const bool ok2 = swj::bind(img, jd, desktop_binding(200), false, why);
        check("SDW-08",
              "desktop binding: after the image's mtime moves its journal is refused, the "
              "reason naming the mtime, and the file is untouched",
              !ok2 && has(why, "mtime") && slurp(jd) == dbytes, why);
        swj::forget_all_for_test();

        // SDW-10: a file that is not a journal (the image itself, by mistake).
        const Bytes imgbytes = slurp(img);
        const bool n1 = swj::bind(img, img, digest_binding(0xAB), false, why);
        const std::string why1 = why;
        const bool n2 = swj::bind(img, img, digest_binding(0xAB), true, why);
        check("SDW-10",
              "a file that is not a journal (wrong magic) is refused at bind with and without "
              "reset, never removed or written",
              !n1 && !n2 && has(why1, "not a jnext SD-writes file") &&
              has(why, "not a jnext SD-writes file") && slurp(img) == imgbytes, why);
        swj::forget_all_for_test();
    }

    // SDW-09: an interrupted append.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        Bytes f = expected_header(b);
        add_record(f, 10, payload(8));
        add_record(f, 11, payload(9));
        add_record(f, 12, payload(10));
        for (int i = 0; i < 100; ++i) f.push_back(0x77);    // a torn fourth record
        {
            std::ofstream o(jf, std::ios::binary);
            o.write(reinterpret_cast<const char*>(f.data()), static_cast<std::streamsize>(f.size()));
        }
        std::string why;
        const bool ok = swj::bind(img, jf, b, false, why);
        bool served = false;
        size_t after = 0;
        {
            Card c(img);
            served = reads(c.sd, 10, payload(8)) && reads(c.sd, 12, payload(10));
            (void)write_block(c.sd, 40, payload(11));
            after = slurp(jf).size();
        }
        check("SDW-09",
              "a trailing partial record is dropped, the whole records before it are served, "
              "and the next append lands right after the last whole record",
              ok && served && after == 64 + 4 * 516u,
              "ok=" + std::to_string(ok) + " after=" + std::to_string(after) + " " + why);
        swj::forget_all_for_test();
    }

    // SDW-11 / SDW-12 / SDW-16: reset, clear, the persist hook and the write generation.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        swj::bind(img, jf, b, false, why);
        {
            Card c(img);
            (void)write_block(c.sd, 3, payload(12));
        }
        swj::forget_all_for_test();
        g_hook_count = 0;
        swj::set_persist_hook(count_hook);
        const bool reset_ok = swj::bind(img, jf, b, true, why);
        const bool gone1 = !exists(jf);
        const int hook_at_bind = g_hook_count;
        bool blob_back = false, clear_ok = false;
        int after_write = 0, after_clear = 0;
        {
            Card c(img);
            blob_back = reads(c.sd, 3, sector_bytes(3));
            (void)write_block(c.sd, 3, payload(13));
            after_write = g_hook_count;
            swj::for_image(img)->clear();
            after_clear = g_hook_count;
            clear_ok = !exists(jf) && reads(c.sd, 3, sector_bytes(3));
        }
        check("SDW-11",
              "reset at bind and clear() both remove the file and the image's bytes are served "
              "again; clear() runs the hook once and bind does not run it",
              reset_ok && gone1 && blob_back && clear_ok && hook_at_bind == 0 &&
              after_write == 1 && after_clear == 2,
              "hook_at_bind=" + std::to_string(hook_at_bind) + " after_write=" +
              std::to_string(after_write) + " after_clear=" + std::to_string(after_clear));

        // SDW-12 / SDW-16 on the same bound image.
        g_hook_count = 0;
        const std::uint64_t gen0 = SdCardDevice::host_write_generation();
        uint8_t past = 0;
        bool rd = false;
        int after3 = 0;
        std::uint64_t gen_after_writes = 0, gen_after_refused = 0;
        {
            Card c(img);
            (void)write_block(c.sd, 50, payload(14));
            (void)write_block(c.sd, 51, payload(15));
            (void)write_block(c.sd, 52, payload(16));
            after3 = g_hook_count;
            gen_after_writes = SdCardDevice::host_write_generation();
            past = write_block(c.sd, kSectors + 5, payload(17));
            rd = reads(c.sd, 50, payload(14));
            gen_after_refused = SdCardDevice::host_write_generation();
        }
        check("SDW-12",
              "the persist hook runs once per committed write and never for a refused write "
              "(past the end: R1 PARAMETER_ERROR 0x40, no data phase) or a read",
              after3 == 3 && past == 0x40 && rd && g_hook_count == 3,
              "after3=" + std::to_string(after3) + " past=" + std::to_string(past) +
              " hooks=" + std::to_string(g_hook_count));
        check("SDW-16",
              "host_write_generation() grows by one per committed journal write and not for a "
              "refused one",
              gen_after_writes - gen0 == 3 && gen_after_refused == gen_after_writes,
              "writes=" + std::to_string(gen_after_writes - gen0));
        swj::forget_all_for_test();
    }

    // SDW-13: a write past the end adds no record.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        std::string why;
        swj::bind(img, jf, digest_binding(0xAB), false, why);
        uint8_t tok = 0;
        {
            Card c(img);
            tok = write_block(c.sd, kSectors + 9, payload(18));
        }
        check("SDW-13",
              "in journal mode a write past the end of the image is refused at the command (R1 "
              "PARAMETER_ERROR 0x40, SD spec 7.3.1.4) and adds no record (the file is never created)",
              tok == 0x40 && !exists(jf) && swj::for_image(img)->empty(),
              "tok=" + std::to_string(tok));
        swj::forget_all_for_test();
    }

    // SDW-14: the live machine and the warm-start recorder share one journal.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        std::string why;
        swj::bind(img, d + "/w.jsdw", digest_binding(0xAB), false, why);
        Card live(img), rec(img);
        const Bytes a = payload(19), b = payload(20);
        (void)write_block(live.sd, 60, a);
        const bool x = reads(rec.sd, 60, a);
        (void)write_block(rec.sd, 61, b);
        const bool y = reads(live.sd, 61, b);
        check("SDW-14",
              "two devices mounting the bound image share one journal: a write through one is "
              "read through the other",
              x && y);
        live.sd.unmount();
        rec.sd.unmount();
        swj::forget_all_for_test();
    }

    // SDW-15: another image mounts without a journal; the bound one gets it back.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string img2 = make_image("c" + std::to_string(n_dir) + "b.img");
        std::string why;
        swj::bind(img, d + "/w.jsdw", digest_binding(0xAB), false, why);
        const Bytes before1 = slurp(img), before2 = slurp(img2);
        const Bytes w = payload(21);
        Card c(img);
        (void)write_block(c.sd, 4, payload(22));
        c.sd.mount(img2, false);
        init_card(c.sd, true);
        (void)write_block(c.sd, 3, w);
        const bool second_written = slurp(img2) != before2 && slurp(img) == before1;
        c.sd.mount(img, false);
        init_card(c.sd, true);
        (void)write_block(c.sd, 6, payload(23));
        const bool journal_back = slurp(img) == before1 && reads(c.sd, 4, payload(22)) &&
                                  reads(c.sd, 6, payload(23));
        check("SDW-15",
              "a live SD change to another image mounts it without a journal (its writes reach "
              "its own file); remounting the bound image restores journal mode",
              second_written && journal_back);
        c.sd.unmount();
        swj::forget_all_for_test();
    }

    // SDW-17: the registry finds the bound image under another spelling.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string other = d + "/../" + fs::path(img).filename().string();
        std::string why;
        const bool bound = swj::bind(other, d + "/w.jsdw", digest_binding(0xAB), false, why);
        const Bytes before = slurp(img);
        const Bytes w = payload(24);
        bool rb = false;
        {
            Card c(img);   // mounted under the plain spelling
            (void)write_block(c.sd, 12, w);
            rb = reads(c.sd, 12, w);
        }
        check("SDW-17",
              "an image bound under one spelling of its path (dir/../x.img) and mounted "
              "under another is still in journal mode: the write reads back and the image file "
              "is byte-identical",
              bound && rb && slurp(img) == before, why);
        swj::forget_all_for_test();
    }

    // SDW-18: a byte-addressed write at an unaligned address is refused.
    {
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        std::string why;
        swj::bind(img, jf, digest_binding(0xAB), false, why);
        uint8_t tok = 0;
        {
            Card c(img, false);
            tok = write_block(c.sd, 7 * 512 + 8, payload(25));
        }
        check("SDW-18",
              "SDSC card in journal mode: CMD24 at byte 7*512+8 (not a block boundary, "
              "WRITE_BL_MISALIGN = 0) is refused at the command (R1 ADDRESS_ERROR 0x20, "
              "SD spec 7.3.1.4) and adds no record",
              tok == 0x20 && !exists(jf) && swj::for_image(img)->empty(),
              "tok=" + std::to_string(tok));
        swj::forget_all_for_test();
    }

    // SDW-19..SDW-28: the file the esxDOS stand-in keeps (a kept-file record).
    const Bytes kd1 = {0x01};
    const Bytes kd2 = {0x02, 0x03};
    const std::string kname = "/JNHI.DAT";
    {   // SDW-19 / SDW-20: the first commit creates the file; a reload gets it back.
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        swj::bind(img, jf, b, false, why);
        const bool c1 = swj::for_image(img)->commit_file(kname, kd1);
        Bytes want = expected_header(b);
        add_kept(want, kname, kd1);
        check("SDW-19",
              "commit_file on a journal with no file creates FILE = header + one kept-file "
              "record (0xFFFFFFFF, L, n, name, bytes), as the format table lays it out",
              c1 && slurp(jf) == want && want.size() == 64 + 4 + 4 + 2 + 9 + 1);
        swj::forget_all_for_test();
        const bool rebound = swj::bind(img, jf, b, false, why);
        const auto kf = swj::for_image(img)->kept_file();
        bool blob = false;
        {
            Card c(img);
            blob = reads(c.sd, 5, sector_bytes(5));
        }
        check("SDW-20",
              "a fresh bind of that FILE (a reload) gives the kept file back (name and bytes); "
              "no sector is journaled (empty()) and every sector reads the image",
              rebound && kf && kf->name == kname && kf->data == kd1 &&
              swj::for_image(img)->empty() && blob && slurp(jf) == want, why);
        swj::forget_all_for_test();
    }
    {   // SDW-21 / SDW-22: later supersedes earlier; compaction order.
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        swj::bind(img, jf, b, false, why);
        {
            Card c(img);
            (void)write_block(c.sd, 9, payload(30));
            (void)swj::for_image(img)->commit_file(kname, kd1);
            (void)swj::for_image(img)->commit_file(kname, kd2);
            (void)write_block(c.sd, 9, payload(31));
        }
        swj::forget_all_for_test();
        const bool rebound = swj::bind(img, jf, b, false, why);
        const auto kf = swj::for_image(img)->kept_file();
        bool sect = false;
        {
            Card c(img);
            sect = reads(c.sd, 9, payload(31));
        }
        Bytes want = expected_header(b);
        add_record(want, 9, payload(31));
        add_kept(want, kname, kd2);
        check("SDW-21",
              "sector 9, file v1, file v2, sector 9 again: a rebind serves the second sector copy and "
              "v2, and compacts FILE to one sector record then one kept-file record",
              rebound && sect && kf && kf->data == kd2 && slurp(jf) == want, why);
        // Another name is the same journal's one file: the later record replaces it.
        (void)swj::for_image(img)->commit_file("/OTHER.DAT", kd1);
        swj::forget_all_for_test();
        swj::bind(img, jf, b, false, why);
        const auto kf2 = swj::for_image(img)->kept_file();
        Bytes compact = expected_header(b);
        add_record(compact, 9, payload(31));
        add_kept(compact, "/OTHER.DAT", kd1);
        check("SDW-22",
              "a kept-file record of another name supersedes the earlier one (one file per "
              "journal): after a reload only the later name is kept and FILE is compacted to the "
              "sector record then that one kept-file record",
              kf2 && kf2->name == "/OTHER.DAT" && kf2->data == kd1 && slurp(jf) == compact,
              why);
        swj::forget_all_for_test();
    }
    {   // SDW-23: a torn kept-file tail.
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        Bytes whole = expected_header(b);
        add_kept(whole, kname, kd1);
        Bytes torn = whole;
        add_kept(torn, "/NEW.DAT", Bytes(40, 0x55));
        torn.resize(torn.size() - 5);              // L runs past EOF
        {
            std::ofstream o(jf, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(torn.data()), static_cast<std::streamsize>(torn.size()));
        }
        const bool b1 = swj::bind(img, jf, b, false, why);
        const auto kf = swj::for_image(img)->kept_file();
        const bool cut = slurp(jf) == whole;
        // A tail cut inside the 8-byte head (marker + half of L).
        Bytes head = whole;
        put_le(head, 0xFFFFFFFFull, 4);
        put_le(head, 7, 2);
        {
            std::ofstream o(jf, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));
        }
        swj::forget_all_for_test();
        const bool b2 = swj::bind(img, jf, b, false, why);
        const bool cut2 = slurp(jf) == whole;
        const bool c3 = swj::for_image(img)->commit_file("/NEXT.DAT", kd2);
        Bytes after = expected_header(b);
        add_kept(after, kname, kd1);
        add_kept(after, "/NEXT.DAT", kd2);
        check("SDW-23",
              "a trailing kept-file record that runs past EOF (or is cut inside its marker/length "
              "head) is dropped, the earlier records are served, FILE is cut back to the last whole "
              "record, and the next commit appends there",
              b1 && b2 && kf && kf->name == kname && cut && cut2 && c3 && slurp(jf) == after, why);
        swj::forget_all_for_test();
    }
    {   // SDW-24: inconsistent inner lengths of a COMPLETE kept-file record.
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const swj::Binding b = digest_binding(0xAB);
        bool all = true;
        std::string last;
        for (int variant = 0; variant < 2; ++variant) {
            const std::string jf = d + "/w" + std::to_string(variant) + ".jsdw";
            Bytes f = expected_header(b);
            put_le(f, 0xFFFFFFFFull, 4);
            if (variant == 0) {          // n = 9 > L - 2 = 3
                put_le(f, 5, 4);
                put_le(f, 9, 2);
                f.insert(f.end(), {'a', 'b', 'c'});
            } else {                      // L = 1 < 2
                put_le(f, 1, 4);
                f.push_back(0);
            }
            { std::ofstream o(jf, std::ios::binary | std::ios::trunc);
              o.write(reinterpret_cast<const char*>(f.data()), static_cast<std::streamsize>(f.size())); }
            std::string why;
            const bool r = swj::bind(img, jf, b, false, why);
            all = all && !r && has(why, "is not a jnext SD-writes file") && slurp(jf) == f;
            last = why;
            // --sdcard-writes-reset is the recovery: the header is a journal's, so it is discarded.
            std::string why2;
            const bool r2 = swj::bind(img, jf, b, true, why2);
            all = all && r2 && !exists(jf);
            swj::forget_all_for_test();
        }
        check("SDW-24",
              "a complete kept-file record with n > L-2, or with L < 2, refuses the bind as not an "
              "SD-writes file and leaves FILE untouched; with reset (the file has a journal's "
              "header) the bind discards it and succeeds", all, last);
        swj::forget_all_for_test();
    }
    {   // SDW-25: the hook, an identical re-commit, clear().
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        std::string why;
        swj::bind(img, jf, digest_binding(0xAB), false, why);
        g_hook_count = 0;
        swj::set_persist_hook(count_hook);
        auto j = swj::for_image(img);
        (void)j->commit_file(kname, kd1);
        const int h1 = g_hook_count;
        const uint64_t len1 = fs::file_size(jf);
        (void)j->commit_file(kname, kd1);               // identical
        const int h2 = g_hook_count;
        const bool same_len = fs::file_size(jf) == len1;
        (void)j->commit_file(kname, kd2);
        const int h3 = g_hook_count;
        j->clear();
        check("SDW-25",
              "the persist hook runs once per commit_file; an identical (name, bytes) re-commit "
              "appends nothing and runs no hook; clear() removes FILE and the kept file, hook +1",
              h1 == 1 && h2 == 1 && same_len && h3 == 2 && g_hook_count == 3 && !exists(jf) &&
              !j->kept_file(),
              "hooks " + std::to_string(h1) + "," + std::to_string(h2) + "," + std::to_string(h3) +
              "," + std::to_string(g_hook_count));
        swj::forget_all_for_test();
    }
    {   // SDW-26: reset at bind.
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        const swj::Binding b = digest_binding(0xAB);
        std::string why;
        swj::bind(img, jf, b, false, why);
        {
            Card c(img);
            (void)write_block(c.sd, 4, payload(32));
        }
        (void)swj::for_image(img)->commit_file(kname, kd1);
        swj::forget_all_for_test();
        const bool ok = swj::bind(img, jf, b, true, why);
        bool blob = false;
        {
            Card c(img);
            blob = reads(c.sd, 4, sector_bytes(4));
        }
        check("SDW-26",
              "bind(reset) on a FILE holding a sector record and a kept-file record discards both: "
              "FILE is gone, no kept file, the image's bytes are served",
              ok && !exists(jf) && !swj::for_image(img)->kept_file() && blob, why);
        swj::forget_all_for_test();
    }
    {   // SDW-27: a kept file is not card state.
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        const std::string jf = d + "/w.jsdw";
        std::string why;
        swj::bind(img, jf, digest_binding(0xAB), false, why);
        (void)swj::for_image(img)->commit_file(kname, kd1);
        int calls = 0;
        swj::for_image(img)->for_each_sorted([&](uint32_t, const uint8_t*) { ++calls; });
        check("SDW-27",
              "with only a kept-file record, for_each_sorted calls its function zero times and "
              "empty() is true (the warm-start key never sees the file)",
              calls == 0 && swj::for_image(img)->empty() && exists(jf));
        swj::forget_all_for_test();
    }
    {   // SDW-28: the 2 TiB guard.
        const std::string d = scratch();
        const std::string img = make_image("c" + std::to_string(n_dir) + ".img");
        swj::Binding big = digest_binding(0xAB);
        big.bytes = (1ull << 32) * 512;
        std::string why_big, why_ok;
        const bool r_big = swj::bind(img, d + "/big.jsdw", big, false, why_big);
        swj::forget_all_for_test();
        big.bytes -= 512;
        const bool r_ok = swj::bind(img, d + "/ok.jsdw", big, false, why_ok);
        check("SDW-28",
              "a binding of 2^32 sectors (2 TiB) is refused (sector 0xFFFFFFFF is the kept-file "
              "marker), naming the size; one sector less binds",
              !r_big && has(why_big, "too large") && r_ok, why_big + " / " + why_ok);
        swj::forget_all_for_test();
    }

    fs::remove_all(g_dir);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
