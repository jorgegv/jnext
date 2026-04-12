#include "headless_app.h"
#include "core/log.h"
#include "input/keyboard.h"
#include <cctype>

bool HeadlessApp::init(int argc, char* argv[]) {
    (void)argc; (void)argv;

    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
    if (!emulator_.init(cfg)) {
        Log::platform()->error("Emulator init failed");
        return false;
    }

    running_ = true;
    Log::platform()->info("Headless mode initialized");
    return true;
}

void HeadlessApp::set_pending_inject(const std::string& file, uint16_t org,
                                     uint16_t pc, int delay_frames) {
    inject_file_ = file;
    inject_org_  = org;
    inject_pc_   = pc;
    inject_countdown_ = delay_frames;
    Log::platform()->info("--inject: will load '{}' at {:#06x} (PC={:#06x}) after {} frame(s)",
                           file, org, pc, delay_frames);
}

void HeadlessApp::set_pending_load(const std::string& file, int delay_frames) {
    load_file_ = file;
    load_countdown_ = delay_frames;
    Log::platform()->info("--load: will load '{}' after {} frame(s)", file, delay_frames);
}

void HeadlessApp::set_delayed_screenshot(const std::string& file, int delay_seconds) {
    screenshot_file_ = file;
    screenshot_countdown_ = delay_seconds * 50;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} second(s)",
                           file, delay_seconds);
}

void HeadlessApp::set_delayed_exit(int delay_seconds) {
    exit_countdown_ = delay_seconds * 50;
    Log::platform()->info("--delayed-automatic-exit: will exit after {} second(s)",
                           delay_seconds);
}

// Map a character to ZX Spectrum keyboard matrix position (row, col).
// Returns false if the key is not recognised.
static bool char_to_matrix(char key, int& row, int& col) {
    // Row 0: SHIFT Z X C V
    // Row 1: A S D F G
    // Row 2: Q W E R T
    // Row 3: 1 2 3 4 5
    // Row 4: 0 9 8 7 6
    // Row 5: P O I U Y
    // Row 6: ENTER L K J H
    // Row 7: SPACE SYM M N B
    switch (key) {
        // digits
        case '1': row=3; col=0; return true;
        case '2': row=3; col=1; return true;
        case '3': row=3; col=2; return true;
        case '4': row=3; col=3; return true;
        case '5': row=3; col=4; return true;
        case '6': row=4; col=4; return true;
        case '7': row=4; col=3; return true;
        case '8': row=4; col=2; return true;
        case '9': row=4; col=1; return true;
        case '0': row=4; col=0; return true;
        // row 1 letters
        case 'a': row=1; col=0; return true;
        case 's': row=1; col=1; return true;
        case 'd': row=1; col=2; return true;
        case 'f': row=1; col=3; return true;
        case 'g': row=1; col=4; return true;
        // row 2 letters
        case 'q': row=2; col=0; return true;
        case 'w': row=2; col=1; return true;
        case 'e': row=2; col=2; return true;
        case 'r': row=2; col=3; return true;
        case 't': row=2; col=4; return true;
        // row 5 letters
        case 'p': row=5; col=0; return true;
        case 'o': row=5; col=1; return true;
        case 'i': row=5; col=2; return true;
        case 'u': row=5; col=3; return true;
        case 'y': row=5; col=4; return true;
        // row 6 letters
        case 'l': row=6; col=1; return true;
        case 'k': row=6; col=2; return true;
        case 'j': row=6; col=3; return true;
        case 'h': row=6; col=4; return true;
        // row 0 letters
        case 'z': row=0; col=1; return true;
        case 'x': row=0; col=2; return true;
        case 'c': row=0; col=3; return true;
        case 'v': row=0; col=4; return true;
        // row 7 letters
        case 'm': row=7; col=2; return true;
        case 'n': row=7; col=3; return true;
        case 'b': row=7; col=4; return true;
        // specials
        case ' ': row=7; col=0; return true;  // SPACE
        case '\n': row=6; col=0; return true;  // ENTER
        default: return false;
    }
}

void HeadlessApp::set_delayed_keypress(char key, int delay_seconds) {
    delayed_keys_.push_back({key, delay_seconds * 50});
    Log::platform()->info("--delayed-keypress: will press '{}' after {} second(s)",
                           key, delay_seconds);
}

void HeadlessApp::run() {
    // Apply RZX play/record at startup.
    if (!rzx_play_file_.empty()) {
        if (!emulator_.load_rzx(rzx_play_file_)) {
            Log::platform()->error("RZX: failed to load '{}'", rzx_play_file_);
        }
    }
    if (!rzx_record_file_.empty()) {
        emulator_.start_rzx_recording(rzx_record_file_);
    }

    while (running_) {
        // Apply pending inject.
        if (inject_countdown_ == 0) {
            emulator_.inject_binary(inject_file_, inject_org_, inject_pc_);
            inject_countdown_ = -1;
        } else if (inject_countdown_ > 0) {
            --inject_countdown_;
        }

        // Apply pending load (auto-detect format by extension).
        if (load_countdown_ == 0) {
            std::string ext;
            auto dot = load_file_.rfind('.');
            if (dot != std::string::npos) {
                ext = load_file_.substr(dot);
                for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext == ".tap") {
                emulator_.load_tap(load_file_, !tape_realtime_);
            } else if (ext == ".tzx") {
                emulator_.load_tzx(load_file_, !tape_realtime_);
            } else if (ext == ".sna") {
                emulator_.load_sna(load_file_);
            } else if (ext == ".szx") {
                emulator_.load_szx(load_file_);
            } else if (ext == ".wav") {
                emulator_.load_wav(load_file_);
            } else if (ext == ".rzx") {
                emulator_.load_rzx(load_file_);
            } else {
                emulator_.load_nex(load_file_);
            }
            load_countdown_ = -1;
        } else if (load_countdown_ > 0) {
            --load_countdown_;
        }

        // Delayed keypresses.
        for (auto it = delayed_keys_.begin(); it != delayed_keys_.end(); ) {
            if (it->countdown <= 0) {
                int row, col;
                if (char_to_matrix(it->key, row, col)) {
                    std::vector<Keyboard::AutoKey> keys = {
                        {row, col, -1, -1, 5}  // press for 5 frames
                    };
                    emulator_.keyboard().queue_auto_type(keys);
                    Log::platform()->info("Delayed keypress '{}' injected", it->key);
                }
                it = delayed_keys_.erase(it);
            } else {
                --it->countdown;
                ++it;
            }
        }

        emulator_.run_frame();

        // Delayed screenshot.
        if (screenshot_countdown_ == 0) {
            save_screenshot_png(screenshot_file_, emulator_.get_framebuffer(),
                                emulator_.get_framebuffer_width(),
                                emulator_.get_framebuffer_height());
            screenshot_countdown_ = -1;
        } else if (screenshot_countdown_ > 0) {
            --screenshot_countdown_;
        }

        // Delayed automatic exit.
        if (exit_countdown_ == 0) {
            Log::platform()->info("automatic exit triggered");
            running_ = false;
        } else if (exit_countdown_ > 0) {
            --exit_countdown_;
        }
    }
}

void HeadlessApp::shutdown() {
    // Stop RZX recording if active (writes the file).
    if (emulator_.rzx_recorder().is_recording()) {
        emulator_.stop_rzx_recording();
    }
    Log::platform()->info("Headless mode shutdown");
}
