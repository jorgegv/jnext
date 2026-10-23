#include "core/tap_loader.h"
#include "core/emulator.h"
#include "core/log.h"
#include "core/host_path.h"
#include "core/tape_trap.h"
#include "core/sha256.h"

#include <cstring>
#include <filesystem>
#include <fstream>

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

    // Parse the raw bytes — the loop lives inline in tap_loader.h
    // (parse_blocks) so mmu_test can round-trip TapSaver output through it.
    std::string error;
    if (!parse_blocks(file_data, blocks_,
                      [](const std::string& msg) { Log::emulator()->warn("{}", msg); },
                      &error)) {
        Log::emulator()->error("TAP: '{}' is not a valid TAP file: {}", path, error);
        return false;
    }

    Log::emulator()->info("TAP: loaded '{}' - {} blocks", path, blocks_.size());

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
            Log::emulator()->debug("  block {}: Header - {} \"{}\"", i, type_str, name);
        } else {
            Log::emulator()->debug("  block {}: Data - {} bytes (flag={:#04x})",
                                   i, b.data.size(), b.flag);
        }
    }

    loaded_ = true;
    current_block_ = 0;
    sha256_ = sdcard::sha256_hex(file_data);

    // Extract just the filename for UI display (std::filesystem splits on the host's own
    // separators: a Windows path from `--load C:\...` or a native dialog has
    // backslashes, which rfind('/') never saw).
    filename_ = std::filesystem::path(path).filename().string();
    path_ = jnext::absolute_host_path(path);

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
    // The CPU stands at LD-BREAK (0x056B): the expected flag is in A',
    // LOAD/VERIFY in the carry of F'. The ROM's transfer, checksum and exit
    // are tape_trap::rom_ld_bytes (shared with the TZX loader).
    const TapBlock* block = next_block();
    if (!block) {
        Log::emulator()->warn("TAP: no more blocks on tape");
        tape_trap::rom_ld_bytes(emu, nullptr, 0);
        return false;
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(block->data.size() + 2);
    bytes.push_back(block->flag);
    bytes.insert(bytes.end(), block->data.begin(), block->data.end());
    bytes.push_back(block->checksum);

    auto regs = emu.cpu().get_registers();
    Log::emulator()->debug("TAP trap: flag={:#04x} {} IX={:#06x} DE={:#06x} block={}",
                            regs.AF2 >> 8, (regs.AF2 & 1) ? "LOAD" : "VERIFY", regs.IX,
                            regs.DE, current_block_ - 1);
    tape_trap::rom_ld_bytes(emu, bytes.data(), bytes.size());
    return true;
}


void TapLoader::eject() {
    blocks_.clear();
    current_block_ = 0;
    loaded_ = false;
    playing_ = false;
    held_ = false;
    filename_.clear();
    path_.clear();
    sha256_.clear();
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
    held_ = false;
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
            } else if (gap_hold_) {
                // Gap boundary with nobody listening: hold the deck here.
                playing_ = false;
                held_ = true;
                play_phase_ = PlayPhase::DONE;
                Log::emulator()->info("TAP: deck held before block {}", current_block_);
                return ear_bit_;
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

// ---------------------------------------------------------------------------
// The deck's position, for a `.jns` (GH #402)
// ---------------------------------------------------------------------------

static const char* const names[] = {"leader", "sync1", "sync2", "data", "pause", "done"};

TapLoader::DeckState TapLoader::deck_state() const {
    DeckState d;
    d.block       = static_cast<uint32_t>(current_block_);
    d.playing     = playing_;
    d.held        = held_;
    d.phase       = names[static_cast<int>(play_phase_)];
    d.pulse_left  = static_cast<uint32_t>(phase_tstates_);
    d.leader_left = static_cast<uint32_t>(leader_pulses_ > 0 ? leader_pulses_ : 0);
    d.byte        = static_cast<uint32_t>(play_byte_idx_);
    d.bit         = static_cast<uint32_t>(play_bit_idx_ > 0 ? play_bit_idx_ : 0);
    d.half        = static_cast<uint32_t>(play_pulse_count_ > 0 ? play_pulse_count_ : 0);
    d.ear         = ear_bit_;
    return d;
}

bool TapLoader::restore_deck(const DeckState& d, std::string& why) {
    if (!loaded_) { why = "no tape is loaded"; return false; }
    int phase = -1;
    for (int i = 0; i < 6; ++i) if (d.phase == names[i]) phase = i;
    if (phase < 0) { why = "unknown phase '" + d.phase + "'"; return false; }
    if (d.block > blocks_.size()) {
        why = "block " + std::to_string(d.block) + " is past the tape's " +
              std::to_string(blocks_.size()) + " blocks";
        return false;
    }
    const auto ph = static_cast<PlayPhase>(phase);
    if (ph == PlayPhase::LEADER || ph == PlayPhase::SYNC1 || ph == PlayPhase::SYNC2 ||
        ph == PlayPhase::DATA) {
        if (d.block >= blocks_.size()) {
            why = "phase '" + d.phase + "' with no block to play";
            return false;
        }
    }
    if (ph == PlayPhase::DATA &&
        d.byte >= 1 + blocks_[d.block].data.size() + 1) {
        why = "byte " + std::to_string(d.byte) + " is past the block's data";
        return false;
    }
    if (d.bit > 7 || d.half > 1 || d.ear > 1) {
        why = "bit, half or ear out of range";
        return false;
    }
    if (d.pulse_left > static_cast<uint32_t>(PAUSE_TSTATES) ||
        d.leader_left > static_cast<uint32_t>(HEADER_LEADER)) {
        why = "pulse or leader count out of range";
        return false;
    }
    if (d.playing && d.held) { why = "the deck is both playing and held"; return false; }

    current_block_    = d.block;
    playing_          = d.playing;
    held_             = d.held;
    play_phase_       = ph;
    phase_tstates_    = d.pulse_left;
    leader_pulses_    = static_cast<int>(d.leader_left);
    play_byte_idx_    = d.byte;
    play_bit_idx_     = static_cast<int>(d.bit);
    play_pulse_count_ = static_cast<int>(d.half);
    ear_bit_          = static_cast<uint8_t>(d.ear);
    return true;
}
