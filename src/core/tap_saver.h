#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class Emulator;

/// TAP file saver for the ZX Spectrum emulator (G33 Phase 1).
///
/// Builds standard TAP blocks from (flag byte, payload) and appends them to
/// an output `.tap` file. A TAP block on file is:
///
///   [2-byte LE length = payload_len + 2] [flag] [payload...] [checksum]
///
/// where checksum = flag XOR payload[0] XOR ... XOR payload[n-1] — exactly
/// the bytes the 48K ROM SA-BYTES routine (0x04C2) sends to tape (flag,
/// data, parity byte), framed with the TAP length prefix.
///
/// The block-building API (`build_block`) and the file-append API
/// (`set_output` / `append_block`) are defined inline in this header — NOT
/// in tap_saver.cpp — so suites that cannot link jnext_core (mmu_test; same
/// precedent as z80_loader.h) can exercise them without an Emulator. Only
/// the ROM-trap glue (`handle_sa_bytes_trap`, which needs Emulator) lives
/// in tap_saver.cpp.
///
/// Output-file semantics: blocks are APPENDED (std::ios::app). An existing
/// file is never truncated — a `SAVE` session extends the tape, mirroring
/// how a real tape (and FUSE's "write to existing tape") behaves.
class TapSaver {
public:
    /// 48K ROM SA-BYTES entry point. Verified against the extracted
    /// /MACHINES/NEXT/48.rom bytes: 0x04C2 = 21 3F 05 E5 (LD HL,0x053F
    /// = SA/LD-RET; PUSH HL) — matching "The Complete Spectrum ROM
    /// Disassembly" (Logan & O'Hara), SA-BYTES.
    static constexpr uint16_t SA_BYTES_ADDR = 0x04C2;

    /// Build one TAP block image from flag + payload.
    /// Returns an empty vector if the block cannot be represented
    /// (payload_len + 2 must fit the 16-bit TAP length field).
    static std::vector<uint8_t> build_block(uint8_t flag,
                                            const uint8_t* payload,
                                            size_t payload_len) {
        std::vector<uint8_t> out;
        if (payload_len + 2 > 0xFFFF) return out;
        const uint16_t block_len = static_cast<uint16_t>(payload_len + 2);
        out.reserve(2 + block_len);
        out.push_back(static_cast<uint8_t>(block_len & 0xFF));
        out.push_back(static_cast<uint8_t>(block_len >> 8));
        out.push_back(flag);
        uint8_t checksum = flag;
        for (size_t i = 0; i < payload_len; ++i) {
            out.push_back(payload[i]);
            checksum ^= payload[i];
        }
        out.push_back(checksum);
        return out;
    }

    /// Set the output .tap path and validate it is appendable.
    /// Returns false (and stays inactive) if the file cannot be opened.
    bool set_output(const std::string& path) {
        std::ofstream f(path, std::ios::binary | std::ios::app);
        if (!f) return false;
        path_ = path;
        return true;
    }

    /// True once an output file has been configured — the SA-BYTES ROM
    /// trap is only armed while active.
    bool active() const { return !path_.empty(); }

    const std::string& output_path() const { return path_; }

    size_t blocks_written() const { return blocks_written_; }

    /// Build a TAP block and append it to the output file.
    /// Returns false if inactive, the block is unrepresentable, or the
    /// file write fails.
    bool append_block(uint8_t flag, const uint8_t* payload, size_t payload_len) {
        if (!active()) return false;
        std::vector<uint8_t> block = build_block(flag, payload, payload_len);
        if (block.empty()) return false;
        std::ofstream f(path_, std::ios::binary | std::ios::app);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(block.data()),
                static_cast<std::streamsize>(block.size()));
        if (!f) return false;
        ++blocks_written_;
        return true;
    }

    /// Handle the ROM trap at SA-BYTES (0x04C2): read the block described
    /// by the CPU registers (A = flag, IX = start, DE = length), append it
    /// as a TAP block, and return to the caller with the success state the
    /// ROM routine's callers expect. Defined in tap_saver.cpp (needs
    /// Emulator). Returns true if a block was written.
    bool handle_sa_bytes_trap(Emulator& emu);

private:
    std::string path_;
    size_t blocks_written_ = 0;
};
