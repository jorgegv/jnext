#pragma once

// GH #366 -- the back/forward history of a debugger view (Disassembly, Source). Pure (no Qt):
// the panel records WHERE it was before a jump and restores it on Back/Forward.
//
//   record(here)    before a jump: push `here` on the back stack and clear the forward stack
//                   (a new jump abandons the old future). A `here` equal to the back stack's
//                   top is not pushed twice. The oldest entry is dropped past kDepth.
//   back(here)      pop the back stack -> return it, and push `here` on the forward stack.
//   forward(here)   the mirror.
//
// `Loc` is whatever the view needs to be put back exactly (an address, or an address pair). It
// needs operator==.

#include <cstddef>
#include <deque>
#include <optional>

namespace jnext {

template <class Loc>
class NavHistory {
public:
    static constexpr size_t kDepth = 100;

    void record(const Loc& here)
    {
        forward_.clear();
        if (!back_.empty() && back_.back() == here) return;
        back_.push_back(here);
        if (back_.size() > kDepth) back_.pop_front();
    }

    std::optional<Loc> back(const Loc& here)
    {
        if (back_.empty()) return std::nullopt;
        Loc to = back_.back();
        back_.pop_back();
        forward_.push_back(here);
        if (forward_.size() > kDepth) forward_.pop_front();
        return to;
    }

    std::optional<Loc> forward(const Loc& here)
    {
        if (forward_.empty()) return std::nullopt;
        Loc to = forward_.back();
        forward_.pop_back();
        back_.push_back(here);
        if (back_.size() > kDepth) back_.pop_front();
        return to;
    }

    bool can_back() const { return !back_.empty(); }
    bool can_forward() const { return !forward_.empty(); }

    void clear()
    {
        back_.clear();
        forward_.clear();
    }

private:
    std::deque<Loc> back_;
    std::deque<Loc> forward_;
};

} // namespace jnext
