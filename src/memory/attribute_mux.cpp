#include "attribute_mux.h"
#include "core/log.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

void AttributeMux::start_frame(const uint8_t* baseline, int hc_origin)
{
    if (baseline) {
        std::memcpy(baseline_.data(), baseline, kNumBytes);
    } else {
        baseline_.fill(0);
    }
    log_size_        = 0;
    overflow_warned_ = false;
    started_         = true;
    hc_origin_       = hc_origin;
    target_line_     = 0;
    for (auto& v : per_offset_log_) v.clear();
    per_offset_cursor_.fill(0);
}

bool AttributeMux::record_write(uint16_t line, uint16_t hc, uint16_t offset, uint8_t value)
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
    const size_t idx = log_size_;
    log_[log_size_++] = Entry{line, hc, offset, value};
    per_offset_log_[offset].push_back(static_cast<uint16_t>(idx));
    return true;
}

void AttributeMux::rewind_to_baseline()
{
    current_     = baseline_;
    target_line_ = 0;
    per_offset_cursor_.fill(0);
}

void AttributeMux::flush_remaining_changes()
{
    // TEMPORARY diagnostic (G12): JNEXT_G12_DUMPOFF=<attr-plane-offset>
    // dumps every recorded write for that offset this frame, with the
    // column-fetch instant it is resolved against.
    static const int dump_off = [] {
        const char* e = std::getenv("JNEXT_G12_DUMPOFF");
        return e ? std::atoi(e) : -1;
    }();
    if (dump_off >= 0 && dump_off < kNumBytes && !per_offset_log_[dump_off].empty()) {
        const int col = dump_off % 32;
        std::fprintf(stderr,
            "[G12-DUMP] off=%d (col=%d row=%d) hc_origin=%d hc_fetch=%d baseline=%02X\n",
            dump_off, col, dump_off / 32, hc_origin_, hc_fetch(col), baseline_[dump_off]);
        for (uint16_t i : per_offset_log_[dump_off]) {
            const Entry& e = log_[i];
            std::fprintf(stderr, "[G12-DUMP]   line=%3u hc=%3u val=%02X\n",
                         e.line, e.hc, e.value);
        }
    }

    // Unconditional drain, ignoring the line/hc gate -- see the header
    // doc comment. Only offsets with a non-empty log do any work.
    for (size_t offset = 0; offset < kNumBytes; ++offset) {
        auto& idx = per_offset_log_[offset];
        size_t& cur = per_offset_cursor_[offset];
        while (cur < idx.size()) {
            current_[offset] = log_[idx[cur]].value;
            ++cur;
        }
    }
}

void AttributeMux::clear()
{
    baseline_.fill(0);
    current_.fill(0);
    log_size_        = 0;
    overflow_warned_ = false;
    started_         = false;
    hc_origin_       = 0;
    target_line_     = 0;
    for (auto& v : per_offset_log_) v.clear();
    per_offset_cursor_.fill(0);
}

