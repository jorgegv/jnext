// Device-boundary audio fill/hold policy test (GitHub issue #208).
//
// Like audio_pacing_test and present_cadence_test this file has NO VHDL
// oracle: it tests a *host* policy, not emulated hardware. Its oracle is the
// written specification in src/platform/audio_fill.h — what the device
// callback is defined to DO when the sample ring cannot cover a request.
// Every row below is derived from that definition, never from reading the
// implementation back.
//
// WHY IT EXISTS. Issue #208: on a host that cannot emulate in real time, the
// tick-side underrun pad (audio_pacing::QUEUE_FLOOR_MS) cannot bridge the
// gaps — it only runs when a tick runs, and the failing hosts are exactly the
// ones whose ticks stall. With the old push-only model SDL then injected
// ZEROS for ~60% of the delivered stream (measured), which over any nonzero
// programme content is the audible click-train / motor noise of issue #7.
// The fix moves the last line of defense into the SDL audio callback, whose
// policy is the pure code under test here. The properties that make the fix a
// fix, each pinned below:
//
//   * a shortfall is filled by repeating the LAST REAL PAIR delivered — a DC
//     hold with no discontinuity — never by zeros;
//   * real samples are always delivered first, in order, byte-intact;
//   * the hold level advances only from real samples (fill output never feeds
//     back into it), and before any real sample it is exact 0 = silence;
//   * the diagnostic counters count pairs exactly, and a fill EVENT is the
//     no-fill -> fill edge, so one starvation is one event.
//
// Run: ./build/test/audio_fill_test

#include "platform/audio_fill.h"

#include <cstdio>
#include <string>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string dump(const audio_fill::State& st)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "last=%d/%d in_fill=%d real=%llu fill=%llu events=%llu",
                  st.last_l, st.last_r, st.in_fill ? 1 : 0,
                  (unsigned long long)st.real_pairs,
                  (unsigned long long)st.fill_pairs,
                  (unsigned long long)st.fill_events);
    return std::string(buf);
}

using audio_fill::fill_request;
using audio_fill::Ring;
using audio_fill::ring_pop;
using audio_fill::ring_push;
using audio_fill::RING_CAPACITY_PAIRS;
using audio_fill::split_request;
using audio_fill::State;
using audio_fill::take_stats;

// A recognisable stereo test signal: L and R are distinct at every index so a
// swap, an off-by-one or a wrap fault cannot cancel out.
int16_t sig_l(int i) { return static_cast<int16_t>(1000 + i); }
int16_t sig_r(int i) { return static_cast<int16_t>(-2000 - i); }

// Push `pairs` pairs of the signal starting at signal index `base`.
int push_sig(Ring& r, int base, int pairs)
{
    // Static: the largest pushes here exceed a comfortable stack frame.
    static int16_t buf[RING_CAPACITY_PAIRS * 2];
    for (int i = 0; i < pairs; i++) {
        buf[i * 2]     = sig_l(base + i);
        buf[i * 2 + 1] = sig_r(base + i);
    }
    return ring_push(r, buf, pairs);
}

}  // namespace

int main()
{
    std::printf("Device-boundary audio fill/hold tests (issue #208)\n");
    std::printf("====================================================\n");

    // --- AFILL-01..04: the pure split ----------------------------------------
    {
        const auto s1 = split_request(100, 500);
        check("AFILL-01a", "full supply: all real, no fill",
              s1.real == 100 && s1.fill == 0);
        const auto s2 = split_request(100, 100);
        check("AFILL-01b", "exact supply: all real, no fill",
              s2.real == 100 && s2.fill == 0);
        const auto s3 = split_request(100, 30);
        check("AFILL-02", "partial supply: real=available, fill=remainder",
              s3.real == 30 && s3.fill == 70);
        const auto s4 = split_request(100, 0);
        check("AFILL-03", "empty ring: all fill",
              s4.real == 0 && s4.fill == 100);
        const auto s5 = split_request(0, 50);
        check("AFILL-04a", "zero request: nothing to do",
              s5.real == 0 && s5.fill == 0);
        const auto s6 = split_request(-5, 50);
        check("AFILL-04b", "negative request clamps to nothing",
              s6.real == 0 && s6.fill == 0);
        const auto s7 = split_request(100, -3);
        check("AFILL-04c", "negative availability clamps to all fill",
              s7.real == 0 && s7.fill == 100);
    }

    // --- AFILL-10: ring basics ------------------------------------------------
    {
        Ring r;
        int16_t out[16] = {1, 1, 1, 1};
        check("AFILL-10a", "a new ring is empty and pop delivers nothing",
              r.size == 0 && ring_pop(r, out, 4) == 0);

        const int acc = push_sig(r, 0, 5);
        check("AFILL-10c", "push into free space accepts everything",
              acc == 5 && r.size == 5);

        const int got = ring_pop(r, out, 5);
        bool intact = (got == 5);
        for (int i = 0; i < 5; i++)
            intact = intact && out[i * 2] == sig_l(i) && out[i * 2 + 1] == sig_r(i);
        check("AFILL-10b", "pop returns the pushed pairs byte-intact, in order",
              intact && r.size == 0);
    }

    // --- AFILL-11: wraparound integrity ---------------------------------------
    // Drive head near the end of the storage, then push a block that straddles
    // the seam. Both channels must cross intact and unswapped.
    {
        Ring r;
        static int16_t out[RING_CAPACITY_PAIRS * 2];
        // Park head at capacity-3: fill, then drain all but nothing — simpler,
        // push capacity-3 pairs and pop them all, leaving head=capacity-3.
        push_sig(r, 0, RING_CAPACITY_PAIRS - 3);
        ring_pop(r, out, RING_CAPACITY_PAIRS - 3);
        // Now 8 pairs straddle the seam (3 before it, 5 after).
        push_sig(r, 100, 8);
        const int got = ring_pop(r, out, 8);
        bool intact = (got == 8);
        bool lr_ok = true;
        for (int i = 0; i < 8; i++) {
            intact = intact && out[i * 2] == sig_l(100 + i) && out[i * 2 + 1] == sig_r(100 + i);
            lr_ok = lr_ok && out[i * 2] > 0 && out[i * 2 + 1] < 0;  // L and R never swapped
        }
        check("AFILL-11a", "a block straddling the wrap seam crosses intact", intact);
        check("AFILL-11b", "L/R are never swapped across the seam", lr_ok);
    }

    // --- AFILL-12..14: ring bounds --------------------------------------------
    {
        Ring r;
        push_sig(r, 0, RING_CAPACITY_PAIRS - 4);   // leave 4 pairs free
        const int acc = push_sig(r, 500, 10);      // ask for 10
        check("AFILL-12a", "an overflowing push accepts only the free space",
              acc == 4 && r.size == RING_CAPACITY_PAIRS);

        // The queued content must be untouched: the original prefix, then the
        // accepted 4 pairs of the second push — nothing overwritten.
        static int16_t out[RING_CAPACITY_PAIRS * 2];
        ring_pop(r, out, RING_CAPACITY_PAIRS - 4);
        bool prefix_ok = out[0] == sig_l(0) &&
                         out[(RING_CAPACITY_PAIRS - 5) * 2] == sig_l(RING_CAPACITY_PAIRS - 5);
        const int got = ring_pop(r, out, 10);
        bool tail_ok = (got == 4);
        for (int i = 0; i < 4; i++)
            tail_ok = tail_ok && out[i * 2] == sig_l(500 + i) && out[i * 2 + 1] == sig_r(500 + i);
        check("AFILL-12b", "overflow truncates the excess; nothing already queued is overwritten",
              prefix_ok && tail_ok);
    }
    {
        Ring r;
        push_sig(r, 0, 3);
        int16_t out[20 * 2];
        const int got = ring_pop(r, out, 20);
        check("AFILL-13", "popping more than queued delivers only what is queued",
              got == 3 && r.size == 0);
    }
    {
        Ring r;
        int16_t buf[4] = {7, 7, 7, 7};
        check("AFILL-14", "zero/negative push and pop are no-ops",
              ring_push(r, buf, 0) == 0 && ring_push(r, buf, -2) == 0 &&
              ring_pop(r, buf, 0) == 0 && ring_pop(r, buf, -2) == 0 && r.size == 0);
    }

    // --- AFILL-20: full supply passthrough ------------------------------------
    {
        Ring r;
        State st;
        push_sig(r, 0, 32);
        int16_t out[32 * 2];
        const auto s = fill_request(r, st, out, 32);
        check("AFILL-20b", "full supply: split is all real, no fill",
              s.real == 32 && s.fill == 0);
        bool intact = true;
        for (int i = 0; i < 32; i++)
            intact = intact && out[i * 2] == sig_l(i) && out[i * 2 + 1] == sig_r(i);
        check("AFILL-20a", "full supply: the device gets exactly the pushed samples", intact);
        check("AFILL-20c", "full supply: no fill counted, no event, not in fill",
              st.real_pairs == 32 && st.fill_pairs == 0 && st.fill_events == 0 &&
              !st.in_fill, dump(st));
    }

    // --- AFILL-21: partial supply — real first, then hold ---------------------
    {
        Ring r;
        State st;
        // Give the state a PRE-callback hold level distinct from the incoming
        // data, so a fill taken from the stale level is distinguishable from
        // one taken from this callback's real tail.
        st.last_l = 12345;
        st.last_r = -12345;
        push_sig(r, 40, 10);                       // 10 real pairs available
        int16_t out[24 * 2];
        const auto s = fill_request(r, st, out, 24);
        check("AFILL-21c", "partial supply: split accounts for every requested pair",
              s.real == 10 && s.fill == 14 && s.real + s.fill == 24);
        bool prefix = true;
        for (int i = 0; i < 10; i++)
            prefix = prefix && out[i * 2] == sig_l(40 + i) && out[i * 2 + 1] == sig_r(40 + i);
        check("AFILL-21a", "partial supply: the real prefix is delivered first, in order",
              prefix);
        bool hold = true;
        for (int i = 10; i < 24; i++)
            hold = hold && out[i * 2] == sig_l(49) && out[i * 2 + 1] == sig_r(49);
        check("AFILL-21b",
              "partial supply: the tail holds THIS callback's last real pair, not the stale level",
              hold, dump(st));
    }

    // --- AFILL-22: starved callbacks hold the last real level -----------------
    {
        Ring r;
        State st;
        push_sig(r, 7, 4);
        int16_t out[16 * 2];
        fill_request(r, st, out, 4);               // consume all 4 — last = sig(10)
        const auto s = fill_request(r, st, out, 16);
        bool held = (s.real == 0 && s.fill == 16);
        for (int i = 0; i < 16; i++)
            held = held && out[i * 2] == sig_l(10) && out[i * 2 + 1] == sig_r(10);
        check("AFILL-22a", "empty ring: the whole request is held at the last real pair",
              held, dump(st));

        // Three more starved callbacks: the level must not decay or drift.
        bool stable = true;
        for (int k = 0; k < 3; k++) {
            fill_request(r, st, out, 8);
            for (int i = 0; i < 8; i++)
                stable = stable && out[i * 2] == sig_l(10) && out[i * 2 + 1] == sig_r(10);
        }
        check("AFILL-22b", "the hold level survives any number of starved callbacks",
              stable, dump(st));
    }

    // --- AFILL-23: before any real sample, the hold is exact silence ----------
    // Silence rests at exact 0 since GH #116, so a callback that beats the
    // first push must deliver zeros — genuine silence, not a click.
    {
        Ring r;
        State st;
        int16_t out[8 * 2];
        for (int i = 0; i < 16; i++) out[i] = 999;  // poison
        const auto s = fill_request(r, st, out, 8);
        bool silent = (s.real == 0 && s.fill == 8);
        for (int i = 0; i < 8; i++)
            silent = silent && out[i * 2] == 0 && out[i * 2 + 1] == 0;
        check("AFILL-23a", "first callback before any real sample fills at exact 0",
              silent, dump(st));
        check("AFILL-23b", "the silent first callback counts as fill, not as real",
              st.real_pairs == 0 && st.fill_pairs == 8 && st.fill_events == 1,
              dump(st));
    }

    // --- AFILL-24: the hold level advances ONLY from real samples -------------
    {
        Ring r;
        State st;
        int16_t out[8 * 2];
        push_sig(r, 0, 4);
        fill_request(r, st, out, 4);
        check("AFILL-24a", "real samples advance the hold level",
              st.last_l == sig_l(3) && st.last_r == sig_r(3), dump(st));
        fill_request(r, st, out, 8);               // starved: 8 fill pairs
        fill_request(r, st, out, 8);               // starved again
        check("AFILL-24b", "fill output never feeds back into the hold level",
              st.last_l == sig_l(3) && st.last_r == sig_r(3), dump(st));
    }

    // --- AFILL-25: a zero-pair request is a true no-op ------------------------
    // It delivers nothing and carries no edge information, so it must neither
    // clear nor arm the fill-event detector.
    {
        Ring r;
        State st;
        int16_t out[4] = {5, 5, 5, 5};
        fill_request(r, st, out, 4);               // empty ring -> in_fill
        const State before = st;
        const auto s = fill_request(r, st, out, 0);
        check("AFILL-25", "a zero-pair request changes nothing, including the edge detector",
              s.real == 0 && s.fill == 0 && st.in_fill == before.in_fill &&
              st.fill_events == before.fill_events &&
              st.real_pairs == before.real_pairs && st.fill_pairs == before.fill_pairs,
              dump(st));
    }

    // --- AFILL-30: the counters count pairs exactly ---------------------------
    {
        Ring r;
        State st;
        static int16_t out[64 * 2];
        push_sig(r, 0, 40);
        fill_request(r, st, out, 24);              // 24 real
        fill_request(r, st, out, 24);              // 16 real + 8 fill
        fill_request(r, st, out, 10);              // 10 fill
        check("AFILL-30a", "real_pairs accumulates exactly the delivered real pairs",
              st.real_pairs == 40, dump(st));
        check("AFILL-30b", "fill_pairs accumulates exactly the manufactured pairs",
              st.fill_pairs == 18, dump(st));
    }

    // --- AFILL-31: a fill EVENT is the no-fill -> fill edge -------------------
    {
        Ring r;
        State st;
        static int16_t out[16 * 2];
        push_sig(r, 0, 16);
        fill_request(r, st, out, 16);              // healthy
        check("AFILL-31a", "healthy callbacks raise no event",
              st.fill_events == 0, dump(st));
        fill_request(r, st, out, 16);              // starved: the edge
        check("AFILL-31b", "the first short callback counts exactly one event",
              st.fill_events == 1, dump(st));
        fill_request(r, st, out, 16);
        fill_request(r, st, out, 16);
        check("AFILL-31c", "a continuing starvation stays ONE event, however long",
              st.fill_events == 1, dump(st));
        push_sig(r, 100, 16);
        fill_request(r, st, out, 16);              // recovery
        push_sig(r, 200, 4);
        fill_request(r, st, out, 16);              // partial: 4 real + 12 fill
        check("AFILL-31d", "a partial callback is a fill callback: recovery + re-starve = second event",
              st.fill_events == 2 && st.in_fill, dump(st));
    }

    // --- AFILL-32: the drain window -------------------------------------------
    {
        Ring r;
        State st;
        static int16_t out[16 * 2];
        push_sig(r, 0, 8);
        fill_request(r, st, out, 16);              // 8 real + 8 fill, 1 event
        const auto w = take_stats(st);
        check("AFILL-32a", "take_stats returns the window and zeroes the counters",
              w.real_pairs == 8 && w.fill_pairs == 8 && w.fill_events == 1 &&
              st.real_pairs == 0 && st.fill_pairs == 0 && st.fill_events == 0,
              dump(st));
        // The starvation is still in progress across the drain: the next short
        // callback must NOT count a new event, or the report would scale with
        // the observer's cadence instead of with what happened.
        fill_request(r, st, out, 16);
        check("AFILL-32b", "a drain mid-starvation does not manufacture a new event",
              st.fill_events == 0 && st.in_fill, dump(st));
        check("AFILL-32c", "a drain does not touch the hold level",
              st.last_l == sig_l(7) && st.last_r == sig_r(7), dump(st));
    }

    // --- AFILL-33: pair integrity through fill_request across the seam --------
    {
        Ring r;
        State st;
        static int16_t out[RING_CAPACITY_PAIRS * 2];
        push_sig(r, 0, RING_CAPACITY_PAIRS - 2);
        ring_pop(r, out, RING_CAPACITY_PAIRS - 2); // park head 2 pairs before the seam
        push_sig(r, 300, 6);                       // straddles the seam
        const auto s = fill_request(r, st, out, 10);
        bool ok = (s.real == 6 && s.fill == 4);
        for (int i = 0; i < 6; i++)
            ok = ok && out[i * 2] == sig_l(300 + i) && out[i * 2 + 1] == sig_r(300 + i);
        for (int i = 6; i < 10; i++)
            ok = ok && out[i * 2] == sig_l(305) && out[i * 2 + 1] == sig_r(305);
        check("AFILL-33", "fill_request across the wrap seam: real pairs intact, hold from the seam's far side",
              ok, dump(st));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
