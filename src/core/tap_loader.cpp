#include "core/tap_loader.h"
#include "core/emulator.h"
#include "core/log.h"

#include <cstring>
#include <fstream>

/// Read a little-endian uint16_t from a byte buffer.
static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

bool TapBlock::verify_checksum() const {
    uint8_t xor_val = flag;
    for (uint8_t b : data) xor_val ^= b;
    xor_val ^= checksum;
    return xor_val == 0;
}

bool TapLoader::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("TAP: cannot open '{}'", path);
        return false;
    }

    auto file_size = f.tellg();
    if (file_size < 2) {
        Log::emulator()->error("TAP: file too small '{}'", path);
        return false;
    }

    f.seekg(0);
    std::vector<uint8_t> file_data(static_cast<size_t>(file_size));
    f.read(reinterpret_cast<char*>(file_data.data()), file_size);

    // Parse TAP blocks: each block is [2-byte LE length][length bytes of data]
    // The data bytes are: [flag][payload...][checksum]
    size_t pos = 0;
    blocks_.clear();

    while (pos + 2 <= file_data.size()) {
        uint16_t block_len = read_u16(&file_data[pos]);
        pos += 2;

        if (block_len == 0) {
            Log::emulator()->warn("TAP: zero-length block at offset {}", pos - 2);
            continue;
        }

        if (pos + block_len > file_data.size()) {
            Log::emulator()->warn("TAP: truncated block at offset {} (need {} bytes, have {})",
                                   pos, block_len, file_data.size() - pos);
            break;
        }

        TapBlock block;
        block.flag = file_data[pos];

        // Data is everything between flag and checksum
        if (block_len >= 2) {
            block.data.assign(&file_data[pos + 1], &file_data[pos + block_len - 1]);
            block.checksum = file_data[pos + block_len - 1];
        } else {
            // Block with only flag byte (unusual but valid — checksum is flag itself)
            block.checksum = block.flag;
        }

        if (!block.verify_checksum()) {
            Log::emulator()->warn("TAP: checksum mismatch in block {} (flag={:#04x})",
                                   blocks_.size(), block.flag);
        }

        blocks_.push_back(std::move(block));
        pos += block_len;
    }

    Log::emulator()->info("TAP: loaded '{}' — {} blocks", path, blocks_.size());

    // Log block details
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& b = blocks_[i];
        if (b.flag == 0x00 && b.data.size() >= 16) {
            // Header block — extract filename and type
            uint8_t type = b.data[0];
            char name[11] = {};
            std::memcpy(name, &b.data[1], 10);
            const char* type_str = "Unknown";
            switch (type) {
                case 0: type_str = "Program"; break;
                case 1: type_str = "Number array"; break;
                case 2: type_str = "Character array"; break;
                case 3: type_str = "Bytes"; break;
            }
            Log::emulator()->info("  block {}: Header — {} \"{}\"", i, type_str, name);
        } else {
            Log::emulator()->info("  block {}: Data — {} bytes (flag={:#04x})",
                                   i, b.data.size(), b.flag);
        }
    }

    loaded_ = true;
    current_block_ = 0;
    return true;
}

const TapBlock* TapLoader::next_block() {
    if (current_block_ >= blocks_.size()) return nullptr;
    return &blocks_[current_block_++];
}

const TapBlock* TapLoader::peek_block() const {
    if (current_block_ >= blocks_.size()) return nullptr;
    return &blocks_[current_block_];
}

bool TapLoader::handle_ld_bytes_trap(Emulator& emu) {
    // LD-BYTES ROM routine interface:
    //   Entry: A = flag byte expected, carry = LOAD(1)/VERIFY(0)
    //          IX = destination address, DE = block length
    //   Exit:  carry set = success, carry reset = error
    //          IX advanced past loaded data, DE = 0

    auto regs = emu.cpu().get_registers();

    // Extract parameters from registers
    uint8_t expected_flag = static_cast<uint8_t>(regs.AF >> 8);   // A = high byte of AF
    bool is_load = (regs.AF & 0x01) != 0;  // carry flag = bit 0 of F
    uint16_t dest = regs.IX;
    uint16_t length = regs.DE;

    Log::emulator()->debug("TAP trap: flag={:#04x} {} IX={:#06x} DE={:#06x} block={}",
                            expected_flag, is_load ? "LOAD" : "VERIFY", dest, length,
                            current_block_);

    // Get next block from tape
    const TapBlock* block = next_block();
    if (!block) {
        Log::emulator()->warn("TAP: no more blocks on tape");
        // Set carry = 0 (error) and return
        regs.AF &= ~0x0001;  // clear carry
        // Pop return address and set PC
        uint16_t ret_lo = emu.mmu().read(regs.SP);
        uint16_t ret_hi = emu.mmu().read(regs.SP + 1);
        regs.SP += 2;
        regs.PC = static_cast<uint16_t>(ret_lo | (ret_hi << 8));
        emu.cpu().set_registers(regs);
        return false;
    }

    // Check flag byte matches
    if (block->flag != expected_flag) {
        Log::emulator()->debug("TAP: flag mismatch (expected {:#04x}, got {:#04x}), skipping",
                                expected_flag, block->flag);
        // The ROM would keep looking for the right block — we set error and let it retry
        regs.AF &= ~0x0001;  // clear carry (error)
        // Pop return address and set PC
        uint16_t ret_lo = emu.mmu().read(regs.SP);
        uint16_t ret_hi = emu.mmu().read(regs.SP + 1);
        regs.SP += 2;
        regs.PC = static_cast<uint16_t>(ret_lo | (ret_hi << 8));
        emu.cpu().set_registers(regs);
        return true;  // trap handled, but load failed
    }

    if (is_load) {
        // LOAD mode: copy data into memory at IX
        size_t copy_len = std::min(static_cast<size_t>(length), block->data.size());
        for (size_t i = 0; i < copy_len; ++i) {
            emu.mmu().write(static_cast<uint16_t>(dest + i), block->data[i]);
        }
        Log::emulator()->debug("TAP: loaded {} bytes to {:#06x}", copy_len, dest);

        // Advance IX and zero DE (as the ROM would)
        regs.IX = static_cast<uint16_t>(dest + copy_len);
        regs.DE = 0;
    } else {
        // VERIFY mode: compare data (just succeed without actually comparing)
        Log::emulator()->debug("TAP: verify mode — auto-passing");
        regs.IX = static_cast<uint16_t>(dest + length);
        regs.DE = 0;
    }

    // Set carry = 1 (success)
    regs.AF |= 0x0001;

    // Pop return address from stack and set PC (simulating RET)
    uint16_t ret_lo = emu.mmu().read(regs.SP);
    uint16_t ret_hi = emu.mmu().read(regs.SP + 1);
    regs.SP += 2;
    regs.PC = static_cast<uint16_t>(ret_lo | (ret_hi << 8));

    emu.cpu().set_registers(regs);
    return true;
}
