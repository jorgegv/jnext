#include "im2.h"
#include "core/saveable.h"

void Im2Controller::reset() {
    for (int i = 0; i < N; ++i) { pending_[i] = false; active_[i] = false; }
    active_level_ = -1;
}

void Im2Controller::raise(Im2Level level) { pending_[static_cast<int>(level)] = true; }
void Im2Controller::clear(Im2Level level) { pending_[static_cast<int>(level)] = false; }

bool Im2Controller::has_pending() const {
    for (int i = 0; i < N; ++i)
        if (pending_[i] && (mask_ & (1u << i))) return true;
    return false;
}

uint8_t Im2Controller::get_vector() const {
    for (int i = 0; i < N; ++i)
        if (pending_[i] && (mask_ & (1u << i)))
            return static_cast<uint8_t>(i * 2);
    return 0xFF;
}

void Im2Controller::set_mask(uint16_t mask) { mask_ = mask; }

void Im2Controller::on_reti() {
    if (active_level_ >= 0) { active_[active_level_] = false; active_level_ = -1; }
}

void Im2Controller::save_state(StateWriter& w) const
{
    for (int i = 0; i < N; ++i) w.write_bool(pending_[i]);
    for (int i = 0; i < N; ++i) w.write_bool(active_[i]);
    w.write_u16(mask_);
    w.write_i32(active_level_);
}

void Im2Controller::load_state(StateReader& r)
{
    for (int i = 0; i < N; ++i) pending_[i] = r.read_bool();
    for (int i = 0; i < N; ++i) active_[i]  = r.read_bool();
    mask_         = r.read_u16();
    active_level_ = r.read_i32();
}
