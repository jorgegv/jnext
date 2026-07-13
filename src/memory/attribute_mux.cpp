#include "attribute_mux.h"
#include "core/log.h"

#include <cstring>

void AttributeMux::start_frame(const uint8_t* baseline)
{
    if (baseline) {
        std::memcpy(baseline_.data(), baseline, kNumBytes);
    } else {
        baseline_.fill(0);
    }
    log_size_        = 0;
    render_cursor_   = 0;
    overflow_warned_ = false;
}

bool AttributeMux::record_write(uint16_t line, uint16_t offset, uint8_t value)
{
    if (offset >= kNumBytes) return false;
    if (log_size_ >= kMaxLogEntries) {
        if (!overflow_warned_) {
            Log::memory()->warn(
                "AttributeMux: change-log full at line {} (cap {} per frame); "
                "further attribute writes this frame degrade to a stale "
                "value for the affected byte(s). G12.",
                line, kMaxLogEntries);
            overflow_warned_ = true;
        }
        return false;
    }
    log_[log_size_++] = Entry{line, offset, value};
    return true;
}

void AttributeMux::rewind_to_baseline()
{
    current_       = baseline_;
    render_cursor_ = 0;
}

void AttributeMux::apply_changes_for_line(int line)
{
    while (render_cursor_ < log_size_
        && log_[render_cursor_].line == static_cast<uint16_t>(line)) {
        const Entry& e = log_[render_cursor_];
        current_[e.offset] = e.value;
        ++render_cursor_;
    }
}

void AttributeMux::flush_remaining_changes()
{
    while (render_cursor_ < log_size_) {
        const Entry& e = log_[render_cursor_];
        current_[e.offset] = e.value;
        ++render_cursor_;
    }
}

void AttributeMux::clear()
{
    baseline_.fill(0);
    current_.fill(0);
    log_size_        = 0;
    render_cursor_   = 0;
    overflow_warned_ = false;
}

