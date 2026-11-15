#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/sdcard_provisioner.h"

/// The minimal NextZXOS SD card (GH #458): a 40 MiB FAT32 card with 512-byte clusters that
/// holds only the boot set (TBBLUE.FW, MACHINES, NEXTZXOS, SYS, LICENSE.md, README.md) plus
/// jnext's two patches (the default MACHINES/NEXT/config.ini, and the known 16 KB zx81.rom
/// twice), made from the official specnext.com distribution zip, or from the 1 GB
/// cspect-next-1gb.img inside it.
///
/// ONE implementation for every platform: the make-minimal-card tool (native tests and the
/// regression rows) and the browser build (WebAssembly) use this same class, so the card the
/// tests boot is the card a visitor's browser makes, byte for byte (kDistribution::card_sha256).
///
/// The input is PUSHED in order, from byte 0, in chunks of any size (1 byte to many MiB;
/// the result does not depend on the chunking), so a browser never holds the zip: one single
/// forward pass, a few MiB of state. The bound (state it, BCARD-14 measures it):
///   - zlib inflate state and window, about 45 KiB; a 64 KiB inflate output buffer;
///   - the image's FAT #1 (fat_size x 512 bytes; 128.5 KiB for 26.11; refused above 16 MiB);
///   - the boot set's data and its directories (2.3 MB for 26.11; refused above 32 MiB, which
///     cannot fit a 40 MiB card anyway);
///   - the FatFs work buffer, 64 KiB; the SHA-256 state.
/// Total for 26.11: about 3 MB. The card itself is a FILE (MEMFS in the browser: JS-side
/// storage, not the wasm heap). Inflation stops (InflateDone) as soon as the last wanted
/// cluster has passed (about 166 MB into the image's 1 GiB), after which the rest of the zip
/// is only hashed.
///
/// A single forward pass needs every directory to come before its contents in the image.
/// That holds for 26.11 (measured over the whole tree); a layout that breaks it is refused
/// with a message naming "second pass", and shows loudly when a developer pins a new
/// distribution (make-minimal-card, MINIMG-01).
namespace minimal_card {

/// The identity of the one distribution this release accepts. Defined in
/// sdcard_provisioner.cpp right after kDistroUrl's constants; `url` IS sdcard::kDistroUrl (the
/// literal stays in kDistroUrl, where the test harness's sed reads it).
struct Pins {
    const char* url;          // == sdcard::kDistroUrl
    const char* file;         // "sn-emulator-26.11.zip" (the last path element of url)
    uint64_t    zip_size;
    const char* zip_sha256;
    const char* image_sha256; // the inflated cspect-next-1gb.img
    const char* card_sha256;  // the card this builder makes from it
    int         recipe;       // the card recipe; bump with any change of the card's bytes
};
extern const Pins kDistribution;

enum class Input { Zip, Image };   // the distribution zip, or its already-inflated .img
enum class Status { NeedMore, InflateDone, Done, Error };

/// What is enforced. An empty string / 0 means "not checked" (unpinned: tests, the tool's
/// --unpinned). pinned() gives the production values from kDistribution.
struct Expect {
    std::string file;                 // named in a refusal ("sn-emulator-26.11.zip")
    std::string zip_sha256;           // Input::Zip: the whole zip, checked BEFORE anything is written
    uint64_t    zip_size = 0;
    std::string image_sha256;         // Input::Image: the whole image, likewise
    std::string card_sha256;          // the finished card; a mismatch removes it
    std::string zx81_known_sha256 = sdcard::kZx81Rom16kSha256;  // test seam, as patch_image_fat32
    uint32_t    card_sectors = 40 * 2048;                      // test seam (a too-small card)
};
Expect pinned(Input in);

class Builder {
public:
    Builder(Input in, std::string out_path, Expect expect);
    ~Builder();
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    /// The next bytes of the input, in order, from byte 0. NeedMore: carry on. InflateDone:
    /// everything the card needs has been read; later calls only hash (the rest of the input
    /// must still be fed: the pinned digest covers all of it). Error: see error(); sticky.
    Status feed(const uint8_t* p, size_t n);
    /// All input given: verify the pinned digests, then write the card, then verify it.
    /// Done, or Error (the output file does not exist, or was removed).
    Status finish();

    const std::string& error() const;
    uint64_t bytes_in() const;
    uint64_t inflated() const;       // bytes of the SD image read so far
    std::string card_sha256() const; // after Done

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace minimal_card
