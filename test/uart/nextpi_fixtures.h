// The NextPi fixtures shared by the UART integration suites and nextpi_test
// (GH #331): a fake NextPi mirror (tar/pax/FAT32 builders and a gzip release),
// a per-process tag, and the Pi-audio tone and mixer-level helpers. Moved here
// verbatim from uart_integration_helpers.h and uart_posix_test.cpp so the NextPi
// rows can live in nextpi_test (a suite that also runs on Windows) while the rows
// that stay in uart_posix_test keep using them. Needs the includes of
// uart_integration_includes.h (or nextpi_test.cpp's own list); it defines no test
// infrastructure.
#pragma once
#include "core/emulator.h"
#include "core/nextpi_provisioner.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <zlib.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// The NextPi directory, prepared on first use (nextpi_provisioner)
//
// Offline: the "mirror" is a temporary directory and the download seam copies
// from it, as sdcard_provisioner's tests do. The archive is built here — a
// gzip-compressed tar in GNU format whose image entry is a tiny MBR disk with a
// hand-made FAT32 boot partition holding kernel.img and a long-named
// bcm2708-rpi-zero.dtb — so the whole real code path runs: checksum, tar
// stream, base-256 / pax sizes and names, the FAT32 reader, the install swap.

namespace {

// A tag unique to this process: concurrent runs share the temp directory, and a
// fixed name lets one run replace or delete another's file (or FIFO).
std::string pid_tag() {
#ifdef _WIN32
    return std::to_string(::_getpid());
#else
    return std::to_string(::getpid());
#endif
}


void put16(std::vector<uint8_t>& b, std::size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v); b[at + 1] = static_cast<uint8_t>(v >> 8);
}
void put32(std::vector<uint8_t>& b, std::size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

/// A 64-sector disk: MBR, one FAT32 (type 0x0C) partition at LBA 8 with one
/// sector per cluster, kernel.img (8.3) and bcm2708-rpi-zero.dtb (VFAT long
/// name) in its root directory. Small files only (one cluster each).
std::vector<uint8_t> fake_nextpi_disk(const std::string& kernel, const std::string& dtb) {
    std::vector<uint8_t> d(64 * 512, 0);
    const std::size_t part = 8 * 512;
    d[0x1BE + 4] = 0x0C;                 // FAT32 LBA
    put32(d, 0x1BE + 8, 8);              // first sector
    put32(d, 0x1BE + 12, 56);            // size
    d[510] = 0x55; d[511] = 0xAA;
    put16(d, part + 11, 512);            // bytes per sector
    d[part + 13] = 1;                    // sectors per cluster
    put16(d, part + 14, 1);              // reserved sectors
    d[part + 16] = 1;                    // FATs
    put32(d, part + 32, 56);             // total sectors
    put32(d, part + 36, 1);              // sectors per FAT
    put32(d, part + 44, 2);              // root cluster
    std::memcpy(&d[part + 82], "FAT32   ", 8);
    d[part + 510] = 0x55; d[part + 511] = 0xAA;
    const std::size_t fat = part + 512;  // LBA 9
    put32(d, fat + 0, 0x0FFFFFF8);
    put32(d, fat + 4, 0x0FFFFFFF);
    for (int cl = 2; cl <= 4; ++cl) put32(d, fat + 4 * cl, 0x0FFFFFFF);   // root, kernel, dtb
    const auto cluster = [&](int cl) { return static_cast<std::size_t>((10 + cl - 2) * 512); };

    std::size_t e = cluster(2);
    auto sfn = [&](const char* name11, int cl, std::size_t size) {
        std::memcpy(&d[e], name11, 11);
        d[e + 11] = 0x20;
        put16(d, e + 26, static_cast<uint16_t>(cl));
        put32(d, e + 28, static_cast<uint32_t>(size));
        e += 32;
    };
    sfn("KERNEL  IMG", 3, kernel.size());
    // "bcm2708-rpi-zero.dtb" = 20 chars: two LFN slots, highest first.
    const std::string lfn = "bcm2708-rpi-zero.dtb";
    for (int slot = 2; slot >= 1; --slot) {
        d[e] = static_cast<uint8_t>(slot | (slot == 2 ? 0x40 : 0));
        d[e + 11] = 0x0F;
        static const int offs[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
        for (int k = 0; k < 13; ++k) {
            const std::size_t idx = static_cast<std::size_t>((slot - 1) * 13 + k);
            const uint16_t ch = idx < lfn.size() ? static_cast<uint8_t>(lfn[idx])
                              : idx == lfn.size() ? 0x0000 : 0xFFFF;
            put16(d, e + offs[k], ch);
        }
        e += 32;
    }
    sfn("BCM270~1DTB", 4, dtb.size());
    std::memcpy(&d[cluster(3)], kernel.data(), kernel.size());
    std::memcpy(&d[cluster(4)], dtb.data(), dtb.size());
    return d;
}

/// A tar header: name, size, type. `base256` writes the size in GNU's
/// base-256 form (how a >8 GB image, like NextPi's, is recorded).
std::string tar_header(const std::string& name, uint64_t size, char type, bool base256 = false) {
    std::string h(512, '\0');
    std::memcpy(&h[0], name.data(), std::min<std::size_t>(name.size(), 100));
    std::snprintf(&h[100], 8, "%07o", 0644);
    if (base256) {
        h[124] = static_cast<char>(0x80);
        for (int i = 0; i < 8; ++i) h[135 - i] = static_cast<char>(size >> (8 * i));
    } else {
        std::snprintf(&h[124], 12, "%011llo", static_cast<unsigned long long>(size));
    }
    h[156] = type;
    std::memcpy(&h[257], "ustar  ", 8);                      // GNU magic, like NextPi's
    std::memset(&h[148], ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : h) sum += c;
    std::snprintf(&h[148], 8, "%06o", sum);
    return h;
}
std::string tar_entry(const std::string& name, const std::string& data, char type = '0',
                      bool base256 = false) {
    std::string out = tar_header(name, data.size(), type, base256) + data;
    out.resize((out.size() + 511) / 512 * 512, '\0');
    return out;
}
std::string pax_record(const std::string& key, const std::string& value) {
    const std::string body = " " + key + "=" + value + "\n";
    std::size_t len = body.size() + 1;
    while (std::to_string(len).size() + body.size() != len) ++len;
    return std::to_string(len) + body;
}

/// NextPi-<release>.tar.gz as the mirror serves it: a directory, an md5 file,
/// a pax-named and base-256-sized image, written with zlib.
bool write_fake_release(const std::string& path, const std::string& release,
                        const std::vector<uint8_t>& disk) {
    const std::string top = "NextPi-" + release + "/";
    std::string tar = tar_entry(top, "", '5');
    tar += tar_entry(top + "NextPi2-test.img.md5", "0123 x\n");
    tar += tar_entry(top + "PaxHeaders/img", pax_record("path", top + "NextPi2-test.img"), 'x');
    tar += tar_entry("ignored-short-name", std::string(disk.begin(), disk.end()), '0', true);
    tar += std::string(1024, '\0');
    gzFile gz = gzopen(path.c_str(), "wb");
    if (!gz) return false;
    const bool ok = gzwrite(gz, tar.data(), static_cast<unsigned>(tar.size())) == static_cast<int>(tar.size());
    return gzclose(gz) == Z_OK && ok;
}

/// A mirror directory plus a download seam that serves from it (and the
/// listing page for "<mirror>/"), counting what it was asked for.
struct FakeMirror {
    std::filesystem::path dir;
    std::string           listing;
    std::vector<std::string> fetched;
    bool                  offline = false;

    explicit FakeMirror(const std::string& tag) {
        dir = std::filesystem::temp_directory_path() / ("jnext-nextpi-mirror-" + tag + "-" + pid_tag());
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }
    ~FakeMirror() { std::error_code ec; std::filesystem::remove_all(dir, ec); }

    std::string url() const { return "https://mirror.test/NextPi2"; }

    bool add_release(const std::string& release, const std::vector<uint8_t>& disk) {
        const std::string archive = (dir / ("NextPi-" + release + ".tar.gz")).string();
        if (!write_fake_release(archive, release, disk)) return false;
        std::ofstream(archive + ".md5") << "MD5 (NextPi-" << release << ".tar.gz) = "
                                        << nextpi::md5_file(archive) << "\n";
        listing += "<a href=\"NextPi-" + release + ".tar.gz\">NextPi-" + release + ".tar.gz</a>\n"
                   "<a href=\"NextPi-" + release + ".tar.gz.md5\">md5</a>\n";
        return true;
    }

    sdcard::DownloadFn download() {
        return [this](const std::string& u, const std::string& dest, const sdcard::ProgressFn& progress,
                      std::string& err) {
            fetched.push_back(u);
            if (offline) { err = "Could not resolve hostname"; return false; }
            if (u == url() + "/") { std::ofstream(dest) << listing; return true; }
            const std::string name = u.substr(u.find_last_of('/') + 1);
            std::error_code ec;
            std::filesystem::copy_file(dir / name, dest,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) { err = "HTTP status 404"; return false; }
            if (progress) progress(1, 1);
            return true;
        };
    }
};

std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

nextpi::ProvisionOptions nextpi_options(FakeMirror& mirror, const std::string& dir,
                                        const std::string& release, int& confirms, bool answer = true) {
    nextpi::ProvisionOptions o;
    o.dir          = dir;
    o.release      = release;
    o.mirror       = mirror.url();
    o.download     = mirror.download();
    o.confirm      = [&confirms, answer](const std::string&) { ++confirms; return answer; };
    o.space_needed = 0;
    return o;
}


/// A tar.gz of `entries` (already-built tar blocks) plus the end marker.
bool write_tar_gz(const std::string& path, const std::string& tar_body) {
    const std::string tar = tar_body + std::string(1024, '\0');
    gzFile gz = gzopen(path.c_str(), "wb");
    if (!gz) return false;
    const bool ok = gzwrite(gz, tar.data(), static_cast<unsigned>(tar.size())) == static_cast<int>(tar.size());
    return gzclose(gz) == Z_OK && ok;
}


/// A WAV file as QEMU's wav back-end writes it (44-byte header, s16 stereo,
/// 44.1 kHz) holding `frames` frames of a square wave flipping every `half`
/// frames: left +amp/-amp and right -r_amp/+r_amp. A right amplitude different
/// from the left one lets a row tell the two channels apart.
void write_tone(const std::string& path, int frames, int16_t amp, int half, int16_t r_amp) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { f.put(static_cast<char>(v)); f.put(static_cast<char>(v >> 8)); };
    f.write("RIFF", 4); u32(0); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
    u32(44100); u32(44100 * 4); u16(4); u16(16); f.write("data", 4); u32(0);
    for (int i = 0; i < frames; ++i) {
        const bool low = (i / half) % 2;
        u16(static_cast<uint16_t>(low ? static_cast<int16_t>(-amp) : amp));
        u16(static_cast<uint16_t>(low ? r_amp : static_cast<int16_t>(-r_amp)));
    }
}

/// The mixer's output over some frames: how many samples, and each channel's
/// lowest and highest value.
struct MixerLevels {
    int n = 0;
    int lo_l = 32767, hi_l = -32768, lo_r = 32767, hi_r = -32768;
};

/// Run `frames` frames with NR 0xA2 held at `a2` and return the levels of the
/// mixer's output over them.
MixerLevels mixer_levels(Emulator& emu, uint8_t a2, int frames) {
    MixerLevels m;
    std::vector<int16_t> buf(2 * 4096);
    for (int f = 0; f < frames; ++f) {
        emu.nextreg().write(0xA2, a2);
        emu.run_frame();
        int n;
        while ((n = emu.mixer().read_samples(buf.data(), 4096)) > 0) {
            m.n += n;
            for (int i = 0; i < n; ++i) {
                m.lo_l = std::min<int>(m.lo_l, buf[2 * i]);     m.hi_l = std::max<int>(m.hi_l, buf[2 * i]);
                m.lo_r = std::min<int>(m.lo_r, buf[2 * i + 1]); m.hi_r = std::max<int>(m.hi_r, buf[2 * i + 1]);
            }
        }
    }
    return m;
}

/// The left and right peak-to-peak of the mixer's output (see mixer_levels).
std::pair<int, int> mixer_swing(Emulator& emu, uint8_t a2, int frames) {
    const MixerLevels m = mixer_levels(emu, a2, frames);
    return {m.hi_l - m.lo_l, m.hi_r - m.lo_r};
}

} // namespace
