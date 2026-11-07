// video_recorder_cmd_test rows that need a child process (SF-08, 09, 11, 12, 13).
//
// These record "successfully" through the stand-in ffmpeg, a program the recorder starts
// as a child process, which the WebAssembly build cannot do, so they moved, IDs,
// fixtures and assertions unchanged, out of video_recorder_cmd_test into a suite that is
// `# os: native` in test/unit-tests.conf. The harness (sf_setup, sf_mode, ...) is shared
// through video_recorder_sf_common.h.
//
// Run: ./build/test/video_recorder_cmd_native_test

#include "core/video_recorder.h"

#include <cstdio>
#include <string>

#include "../row_id.h"
#include "video_recorder_sf_common.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

}  // namespace

using namespace vr_sf;

int main()
{
    const bool sf_ready = sf_setup();
    {
        VideoRecorder rec;
        sf_mode("success");
        const bool started = sf_ready && sf_start_with_frame(rec, "good.mp4");
        const bool stopped = started && rec.stop();
        check("SF-08", "a successful recording: stop() true, stop_failed() stays false",
              started && stopped && !rec.stop_failed());
    }
    {
        // Sticky across a DIFFERENT path: the failed recording at sticky1
        // never materialized; a later success at sticky2 must not clear it.
        // (A success at the SAME path does clear it — SF-12.)
        VideoRecorder rec;
        sf_mode("encode-fail");
        bool ok = sf_ready && sf_start_with_frame(rec, "sticky1.mp4");
        ok = ok && !rec.stop() && rec.stop_failed();
        sf_mode("success");
        ok = ok && sf_start_with_frame(rec, "sticky2.mp4");
        const bool second_ok = ok && rec.stop();
        check("SF-09", "the latch is STICKY: a later successful recording does not clear it",
              ok && second_ok && rec.stop_failed());
    }
    {
        // GH #86 review round 2 BLOCKER row — REVERSE ordering. The CLI
        // file (A) records fine FIRST; an unrelated ad-hoc recording (B)
        // fails LATER. The path-scoped check main.cpp uses must stay clean
        // for A while reporting B — an object-scoped latch cannot pass.
        VideoRecorder rec;
        sf_mode("success");
        bool ok = sf_ready && sf_start_with_frame(rec, "cli_a.mp4");
        ok = ok && rec.stop();
        sf_mode("encode-fail");
        ok = ok && sf_start_with_frame(rec, "adhoc_b.mp4");
        const bool b_failed = ok && !rec.stop();
        check("SF-11", "an unrelated later failure (B) does not mark an earlier successful output (A)",
              ok && b_failed && !rec.output_failed(sf_path("cli_a.mp4")) &&
                  rec.output_failed(sf_path("adhoc_b.mp4")));
    }
    {
        // Same-path retry: a later SUCCESSFUL recording to the SAME path
        // clears that path's failure — the requested artifact now exists.
        VideoRecorder rec;
        sf_mode("encode-fail");
        bool ok = sf_ready && sf_start_with_frame(rec, "retry.mp4");
        ok = ok && !rec.stop() && rec.output_failed(sf_path("retry.mp4"));
        sf_mode("success");
        ok = ok && sf_start_with_frame(rec, "retry.mp4");
        const bool second_ok = ok && rec.stop();
        check("SF-12", "a successful re-record to the SAME path clears that path's failure",
              ok && second_ok && !rec.output_failed(sf_path("retry.mp4")) &&
                  !rec.stop_failed());
    }
    {
        // A PARTIAL, NON-EMPTY artifact from a failing encoder is KEPT for
        // inspection (only 0-byte leftovers are removed) — and the failure
        // still latches.
        VideoRecorder rec;
        sf_mode("partial-fail");
        const bool started = sf_ready && sf_start_with_frame(rec, "partial.mp4");
        const bool stopped = started && rec.stop();
        std::error_code sz_ec;
        const auto sz = fs::file_size(g_sf_dir / "partial.mp4", sz_ec);
        check("SF-13", "a failed encode's partial non-empty output is kept, and still latches",
              started && !stopped && rec.output_failed(sf_path("partial.mp4")) &&
                  !sz_ec && sz > 0);
    }
    if (sf_ready) {
        std::error_code ec;
        fs::remove_all(g_sf_dir, ec);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
