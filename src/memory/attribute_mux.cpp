#include "attribute_mux.h"
#include "core/saveable.h"
#include "core/log.h"

#include <algorithm>
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

void AttributeMux::save_state(StateWriter& w) const
{
    w.write_bytes(baseline_.data(), baseline_.size());
    w.write_bytes(current_.data(), current_.size());
    w.write_u64(static_cast<uint64_t>(log_size_));
    for (size_t i = 0; i < log_size_; ++i) {
        w.write_u16(log_[i].line);
        w.write_u16(log_[i].offset);
        w.write_u8(log_[i].value);
    }
    w.write_u64(static_cast<uint64_t>(render_cursor_));
}

void AttributeMux::load_state(StateReader& r)
{
    r.read_bytes(baseline_.data(), baseline_.size());
    r.read_bytes(current_.data(), current_.size());
    uint64_t n = r.read_u64();
    log_size_ = std::min(static_cast<size_t>(n), kMaxLogEntries);
    for (size_t i = 0; i < log_size_; ++i) {
        Entry e;
        e.line   = r.read_u16();
        e.offset = r.read_u16();
        e.value  = r.read_u8();
        log_[i] = e;
    }
    // If the saved log was truncated to fit kMaxLogEntries, still consume
    // the remaining bytes so the stream stays aligned for whatever follows.
    for (uint64_t i = log_size_; i < n; ++i) {
        r.read_u16(); r.read_u16(); r.read_u8();
    }
    render_cursor_   = static_cast<size_t>(r.read_u64());
    overflow_warned_ = false;
}
