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

    // Extract just the filename for UI display
    auto slash = path.rfind('/');
    filename_ = (slash != std::string::npos) ? path.substr(slash + 1) : path;

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


void TapLoader::eject() {
    blocks_.clear();
    current_block_ = 0;
    loaded_ = false;
    playing_ = false;
    filename_.clear();
    Log::emulator()->info("TAP: tape ejected");
}

// ---------------------------------------------------------------------------
// Real-time tape playback
// ---------------------------------------------------------------------------

void TapLoader::start_realtime_playback() {
    if (!loaded_ || at_end()) return;

    const TapBlock* block = peek_block();
    if (!block) return;

    playing_ = true;
    play_tstates_ = 0;
    play_byte_idx_ = 0;
    play_bit_idx_ = 7;
    play_pulse_count_ = 0;
    ear_bit_ = 0;

    // Leader tone: header blocks get longer pilot tone
    play_phase_ = PlayPhase::LEADER;
    leader_pulses_ = (block->flag == 0x00) ? HEADER_LEADER : DATA_LEADER;
    phase_tstates_ = PILOT_PULSE;

    Log::emulator()->debug("TAP realtime: starting block {} ({} leader pulses)",
                            current_block_, leader_pulses_);
}

uint8_t TapLoader::tick_realtime(uint64_t tstates) {
    if (!playing_) return ear_bit_;

    // Process the given number of T-states
    while (tstates > 0) {
        if (phase_tstates_ <= tstates) {
            tstates -= phase_tstates_;
            phase_tstates_ = 0;
        } else {
            phase_tstates_ -= tstates;
            tstates = 0;
            break;
        }

        // Current pulse completed — toggle EAR and advance state
        ear_bit_ ^= 1;

        switch (play_phase_) {
        case PlayPhase::LEADER:
            --leader_pulses_;
            if (leader_pulses_ <= 0) {
                play_phase_ = PlayPhase::SYNC1;
                phase_tstates_ = SYNC1_PULSE;
            } else {
                phase_tstates_ = PILOT_PULSE;
            }
            break;

        case PlayPhase::SYNC1:
            play_phase_ = PlayPhase::SYNC2;
            phase_tstates_ = SYNC2_PULSE;
            break;

        case PlayPhase::SYNC2:
            play_phase_ = PlayPhase::DATA;
            play_byte_idx_ = 0;
            play_bit_idx_ = 7;
            play_pulse_count_ = 0;
            // First data byte is the flag byte
            {
                const TapBlock* block = peek_block();
                if (!block) {
                    play_phase_ = PlayPhase::DONE;
                    phase_tstates_ = 0;
                    break;
                }
                // Prepare to send flag byte, then data, then checksum
                // We'll send: flag, data[0..n-1], checksum
                // Total bytes = 1 + data.size() + 1
                uint8_t byte = block->flag;  // start with flag
                int bit = (byte >> play_bit_idx_) & 1;
                phase_tstates_ = bit ? ONE_PULSE : ZERO_PULSE;
            }
            break;

        case PlayPhase::DATA: {
            const TapBlock* block = peek_block();
            if (!block) {
                play_phase_ = PlayPhase::DONE;
                break;
            }

            ++play_pulse_count_;
            if (play_pulse_count_ < 2) {
                // Second half of the same bit — same duration
                uint8_t byte;
                size_t total_bytes = 1 + block->data.size() + 1; // flag + data + checksum
                if (play_byte_idx_ == 0)
                    byte = block->flag;
                else if (play_byte_idx_ <= block->data.size())
                    byte = block->data[play_byte_idx_ - 1];
                else
                    byte = block->checksum;
                int bit = (byte >> play_bit_idx_) & 1;
                phase_tstates_ = bit ? ONE_PULSE : ZERO_PULSE;
            } else {
                // Bit complete — move to next bit
                play_pulse_count_ = 0;
                --play_bit_idx_;
                if (play_bit_idx_ < 0) {
                    play_bit_idx_ = 7;
                    ++play_byte_idx_;
                    size_t total_bytes = 1 + block->data.size() + 1;
                    if (play_byte_idx_ >= total_bytes) {
                        // Block complete — pause then next block
                        play_phase_ = PlayPhase::PAUSE;
                        phase_tstates_ = PAUSE_TSTATES;
                        next_block();  // advance to next block
                        break;
                    }
                }
                // Set up next bit pulse
                uint8_t byte;
                if (play_byte_idx_ == 0)
                    byte = block->flag;
                else if (play_byte_idx_ <= block->data.size())
                    byte = block->data[play_byte_idx_ - 1];
                else
                    byte = block->checksum;
                int bit = (byte >> play_bit_idx_) & 1;
                phase_tstates_ = bit ? ONE_PULSE : ZERO_PULSE;
            }
            break;
        }

        case PlayPhase::PAUSE:
            if (at_end()) {
                play_phase_ = PlayPhase::DONE;
                playing_ = false;
                Log::emulator()->info("TAP realtime: tape ended");
            } else {
                // Start next block
                start_realtime_playback();
            }
            break;

        case PlayPhase::DONE:
            playing_ = false;
            return ear_bit_;
        }
    }

    return ear_bit_;
}
