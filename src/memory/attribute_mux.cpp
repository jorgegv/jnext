#include "attribute_mux.h"

#include <algorithm>
#include <cstring>

void AttributeMux::reset_touched_()
{
    for (const uint32_t o : touched_) { st_[o] = OffState{}; hot_[o] = 0; }
    touched_.clear();
    linked_ = 0;
}

void AttributeMux::start_frame(const uint8_t* baseline, int hc_origin,
                               uint32_t plane_bytes)
{
    if (plane_bytes != nbytes_) {
        nbytes_ = plane_bytes;
        base_.assign(nbytes_, 0);
        st_.assign(nbytes_, OffState{});
        hot_.assign(nbytes_, 0);
        touched_.clear();
        linked_ = 0;
    } else {
        reset_touched_();
    }
    if (baseline) {
        std::memcpy(base_.data(), baseline, nbytes_);
    } else {
        std::fill(base_.begin(), base_.end(), 0);
    }
    log_.clear();
    started_     = true;
    hc_origin_   = hc_origin;
    target_line_ = 0;
}

bool AttributeMux::record_write(uint16_t line, uint16_t hc, uint32_t offset, uint8_t value)
{
    if (offset >= nbytes_) return false;
    log_.push_back(Entry{(static_cast<uint32_t>(line) << 16) | hc, kNone,
                         static_cast<uint16_t>(offset), value});
    return true;
}

void AttributeMux::link_pending_() const
{
    for (; linked_ < log_.size(); ++linked_) {
        const uint32_t n = static_cast<uint32_t>(linked_);
        const uint32_t offset = log_[n].offset;
        OffState& s = st_[offset];
        if (!hot_[offset]) {
            s.first   = n;
            s.last    = n;
            s.cursor  = n;
            s.lastkey = 0;
            s.curval  = base_[offset];
            hot_[offset] = 1;
            touched_.push_back(offset);
        } else {
            log_[s.last].next = n;
            s.last = n;
            if (s.cursor == kNone) s.cursor = n;
        }
    }
}

void AttributeMux::rewind_to_baseline()
{
    target_line_ = 0;
    for (const uint32_t o : touched_) {
        OffState& s = st_[o];
        s.cursor  = s.first;
        s.curval  = base_[o];
        s.lastkey = 0;
    }
}

void AttributeMux::flush_remaining_changes()
{
    link_pending_();
    for (const uint32_t o : touched_) {
        OffState& s = st_[o];
        while (s.cursor != kNone) {
            s.curval = log_[s.cursor].value;
            s.cursor = log_[s.cursor].next;
        }
    }
}

void AttributeMux::clear()
{
    nbytes_ = 0;
    base_.clear();
    st_.clear();
    hot_.clear();
    log_.clear();
    touched_.clear();
    linked_      = 0;
    started_     = false;
    hc_origin_   = 0;
    target_line_ = 0;
}
