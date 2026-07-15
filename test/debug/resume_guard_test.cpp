// ResumeGuard unit tests — Task 60e (2026-07-15).
//
// ResumeGuard is the pure, GUI-free policy behind DebuggerManager's single
// corruption choke point (confirm_resume_if_corrupt). It decides, from two
// Emulator observables — `corrupt` (last_state_error() non-empty) and
// `generation` (state_error_generation(), monotonic, bumped per fresh
// corruption) — whether a resume/step must prompt the user.
//
// The Qt dialog itself cannot be driven headlessly (modal, needs a display +
// user click), so the SLOT wiring is verified by code inspection + the
// build; this suite pins the DECISION LOGIC every slot depends on, so a
// regression that (say) stops re-prompting on a new incident, or that lets a
// clean machine prompt, fails loudly here.
//
// Run: ./build/test/resume_guard_test

#include "debug/resume_guard.h"

#include <cstdio>

// ── Tiny test harness (matches profiler_test.cpp style) ────────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

int main() {
    std::printf("\n======================================================\n");
    std::printf("ResumeGuard unit tests (Task 60e, 2026-07-15)\n");
    std::printf("======================================================\n\n");

    // RG-01 — a clean machine never prompts, whatever the generation value.
    {
        ResumeGuard g;
        check("RG-01a", "clean (gen 0) → no confirmation",
              !g.needs_confirmation(/*corrupt=*/false, 0));
        check("RG-01b", "clean (gen 7) → no confirmation",
              !g.needs_confirmation(false, 7));
    }

    // RG-02 — corrupt and never acknowledged → must prompt.
    {
        ResumeGuard g;
        check("RG-02", "corrupt, un-acked → needs confirmation",
              g.needs_confirmation(true, 1));
    }

    // RG-03 — after acknowledging THIS incident, the same generation is silent.
    {
        ResumeGuard g;
        check("RG-03a", "corrupt gen 3 first ask → prompt",
              g.needs_confirmation(true, 3));
        g.record_ack(3);
        check("RG-03b", "corrupt gen 3 after ack → no prompt",
              !g.needs_confirmation(true, 3));
        check("RG-03c", "ack is idempotent on repeat query",
              !g.needs_confirmation(true, 3));
    }

    // RG-04 — a NEW corruption (higher generation) re-prompts even though an
    // earlier incident was acknowledged. This is the crux of finding 5:
    // acknowledgment is per-incident, not a permanent silence.
    {
        ResumeGuard g;
        g.record_ack(3);
        check("RG-04", "corrupt gen 4 after acking gen 3 → prompt again",
              g.needs_confirmation(true, 4));
    }

    // RG-05 — going clean after an ack still never prompts (corrupt short-
    // circuits before the generation is even consulted).
    {
        ResumeGuard g;
        g.record_ack(2);
        check("RG-05", "acked then clean → no prompt",
              !g.needs_confirmation(false, 2));
    }

    // RG-06 — the generation-0 collision guard: the default acked_generation_
    // is 0, but a corrupt gen-0 incident must STILL prompt until explicitly
    // acked (proves the code gates on the acked flag, not just the number).
    {
        ResumeGuard g;
        check("RG-06a", "corrupt gen 0, never acked → prompt",
              g.needs_confirmation(true, 0));
        g.record_ack(0);
        check("RG-06b", "corrupt gen 0 after ack → no prompt",
              !g.needs_confirmation(true, 0));
    }

    // RG-07 — acking a newer incident does not retroactively silence an older
    // generation value (defensive: generations are monotonic in practice, but
    // the predicate must be an exact match, not >=).
    {
        ResumeGuard g;
        g.record_ack(5);
        check("RG-07", "corrupt gen 4 while acked gen 5 → prompt",
              g.needs_confirmation(true, 4));
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
