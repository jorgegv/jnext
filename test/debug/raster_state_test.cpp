// RasterState unit tests — GH #22 (debugger raster + ULA fetch indicator).
//
// The oracle is the FPGA core, not jnext's existing C++:
//   video/zxula_timing.vhd  the frame counters (hc/vc), the ULA counters
//                           (hc_ula/vc_ula), the copper-offset counter (cvc),
//                           the practical pixel counter (phc), and the
//                           per-machine blanking constants.
//   video/zxula.vhd         the display-RAM read schedule and the three
//                           border/blanking gates that decide whether the
//                           fetched byte is used at all.
//   zxnext.vhd              which counter NR 0x1E/0x1F actually reports.
//
// Every row states the file:line it is derived from. The point of the suite is
// that the four counters have four different origins and that the region and
// the fetch state are INDEPENDENT — confusing either pair is what produced
// GH #16 (NR 0x1E/0x1F read as a raw frame line) and GH #181 (the Copper's
// hpos compared against 28 MHz master cycles instead of the 7 MHz hc_ula).
//
// Run: ./build/test/raster_state_test

#include "debug/raster_state.h"
#include "video/timing.h"

#include <cstdio>
#include <string_view>

// ── Tiny test harness (matches resume_guard_test.cpp style) ────────────

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

// A VideoTiming initialised for one timing mode. `init_timing` is the tim_sel
// entry point the emulator itself uses for a runtime NR 0x03 change, so the
// fixture and production configure the model the same way.
static VideoTiming make_timing(MachineTimingMode mode, bool hz60 = false) {
    VideoTiming t;
    t.init_timing(mode, hz60);
    return t;
}

static RasterState at(const VideoTiming& t, int hc, int vc,
                      uint8_t port_ff = 0, bool shadow = false) {
    return raster_state_at(t, hc, vc, port_ff, shadow);
}

int main() {
    std::printf("\n======================================================\n");
    std::printf("RasterState unit tests (GH #22)\n");
    std::printf("======================================================\n\n");

    const VideoTiming t48   = make_timing(MachineTimingMode::Timing48);
    const VideoTiming t128  = make_timing(MachineTimingMode::Timing128);
    const VideoTiming tp3   = make_timing(MachineTimingMode::TimingPlus3);
    const VideoTiming tpent = make_timing(MachineTimingMode::TimingPentagon);
    const VideoTiming t48_60 = make_timing(MachineTimingMode::Timing48, true);

    // ── 1. Per-machine blanking constants ────────────────────────────────
    // `blank_n <= '0' when (hc <= c_max_hblank) or (vc <= c_max_vblank)`
    // (zxula_timing.vhd:348-357). These constants had no C++ home before
    // GH #22 — nothing else in jnext models blanking.

    check("RS-BLANK-01", "48K c_max_hblank = 95 (zxula_timing.vhd:260)",
          t48.max_hblank() == 95);
    check("RS-BLANK-02", "48K c_max_vblank = 7 (zxula_timing.vhd:268)",
          t48.max_vblank() == 7);
    check("RS-BLANK-03", "128K c_max_hblank = 95 (zxula_timing.vhd:194)",
          t128.max_hblank() == 95);
    check("RS-BLANK-04", "128K c_max_vblank = 7 (zxula_timing.vhd:202)",
          t128.max_vblank() == 7);
    check("RS-BLANK-05", "+3 shares the 128K blanking block = 95/7 (zxula_timing.vhd:194,202)",
          tp3.max_hblank() == 95 && tp3.max_vblank() == 7);
    check("RS-BLANK-06", "Pentagon c_max_hblank = 63 (zxula_timing.vhd:158)",
          tpent.max_hblank() == 63);
    check("RS-BLANK-07", "Pentagon c_max_vblank = 15 (zxula_timing.vhd:166)",
          tpent.max_vblank() == 15);
    check("RS-BLANK-08", "60 Hz branch keeps 95/7 (zxula_timing.vhd:228,236 / :288,296)",
          t48_60.max_hblank() == 95 && t48_60.max_vblank() == 7);

    // ── 2. Counter rebases ───────────────────────────────────────────────
    // hc_ula is reset one tick AFTER the combinational compare `hc =
    // c_min_hactive - 12`, because the reset is registered
    // (zxula_timing.vhd:423-436, and :344 "EVERYTHING BELOW DELAYED ONE PIXEL
    // FROM FRAME COUNTER"). So hc_ula == 0 at raw hc c_min_hactive - 11.

    check("RS-HCULA-01", "48K hc_ula = 0 at raw hc 117 = c_min_hactive-11 (zxula_timing.vhd:261,423-436)",
          at(t48, 117, 100).hc_ula == 0);
    check("RS-HCULA-02", "128K hc_ula = 0 at raw hc 125 = c_min_hactive-11 (zxula_timing.vhd:195,423-436)",
          at(t128, 125, 100).hc_ula == 0);
    check("RS-HCULA-03", "Pentagon hc_ula = 0 at raw hc 117 = c_min_hactive-11 (zxula_timing.vhd:159,423-436)",
          at(tpent, 117, 100).hc_ula == 0);
    check("RS-HCULA-04", "48K hc_ula counts up with raw hc within the line (zxula_timing.vhd:427-436)",
          at(t48, 130, 100).hc_ula == 13);
    check("RS-HCULA-05", "48K hc_ula wraps the line: raw hc 116 is the last tick, hc_ula = 447 (zxula_timing.vhd:262,427-436)",
          at(t48, 116, 100).hc_ula == 447);

    // phc: loaded with -48 at raw hc c_min_hactive - 48 and counted up
    // (zxula_timing.vhd:510-522), so it reaches 0 twelve ticks after hc_ula —
    // zxula.vhd:43-46 states the same relation ("0 corresponds to when the
    // system is actually generating pixel 0 ... ULA count i_hc = 0xC").

    check("RS-PHC-01", "48K phc = 0 when hc_ula = 0xC (zxula.vhd:43-46, zxula_timing.vhd:510-522)",
          at(t48, 129, 100).phc == 0 && at(t48, 129, 100).hc_ula == 12);
    check("RS-PHC-02", "48K phc = -12 at hc_ula 0 — the prefetch lead (zxula_timing.vhd:423,513-517)",
          at(t48, 117, 100).phc == -12);
    check("RS-PHC-03", "48K phc reloads to -48 at raw hc c_min_hactive-47 = 81 (zxula_timing.vhd:511-515)",
          at(t48, 81, 100).phc == -48);
    check("RS-PHC-04", "48K phc still counting at raw hc c_min_hactive-48 = 80 (registered load, zxula_timing.vhd:513-517)",
          at(t48, 80, 100).phc == 399);
    check("RS-PHC-05", "128K phc = 0 at raw hc 137 = c_min_hactive+1 (zxula_timing.vhd:195,510-522)",
          at(t128, 137, 100).phc == 0);
    check("RS-PHC-06", "48K last paper pixel phc = 255 at raw hc 384 (zxula.vhd:415)",
          at(t48, 384, 100).phc == 255);

    // The phc reload is per-machine: it lands at raw hc `c_min_hactive - 47`
    // and the counter it wraps against is `c_max_hc + 1`
    // (zxula_timing.vhd:511-517 against :196 / :262 / :160).  The pair of rows
    // per machine straddles that boundary, because a fold threshold or a
    // subtrahend frozen at one machine's constants is invisible to a test that
    // only ever asks that machine.
    check("RS-PHC-07", "128K phc still counting at raw hc 88 = c_min_hactive-48 (zxula_timing.vhd:195,196,513-517)",
          at(t128, 88, 100).phc == 407);
    check("RS-PHC-08", "128K phc reloads to -48 at raw hc 89, folding on c_max_hc+1 = 456 (zxula_timing.vhd:196,511-515)",
          at(t128, 89, 100).phc == -48);
    check("RS-PHC-09", "+3 shares the 128K line geometry: hc 88 -> 407, hc 89 -> -48 (zxula_timing.vhd:195,196)",
          at(tp3, 88, 100).phc == 407 && at(tp3, 89, 100).phc == -48);
    check("RS-PHC-10", "Pentagon folds on c_max_hc+1 = 448: hc 80 -> 399, hc 81 -> -48 (zxula_timing.vhd:159,160)",
          at(tpent, 80, 100).phc == 399 && at(tpent, 81, 100).phc == -48);
    check("RS-PHC-11", "48K 60 Hz keeps c_min_hactive 128 / c_max_hc 447, so the fold is unmoved (zxula_timing.vhd:261,262 vs :289,290)",
          at(t48_60, 80, 100).phc == 399 && at(t48_60, 81, 100).phc == -48);

    // vc_ula is clocked by the SAME ula_max_hc pulse as hc_ula
    // (zxula_timing.vhd:441-451), so an ULA line starts at raw hc
    // c_min_hactive-11, not at raw hc 0.

    check("RS-VCULA-01", "48K vc_ula = 0 on raw line 64 at/after the ULA origin (zxula_timing.vhd:269,441-451)",
          at(t48, 117, 64).vc_ula == 0 && at(t48, 300, 64).vc_ula == 0);
    check("RS-VCULA-02", "48K raw hc before the ULA origin still belongs to the previous ULA line (zxula_timing.vhd:441-451)",
          at(t48, 116, 64).vc_ula == 311);
    check("RS-VCULA-03", "Pentagon vc_ula = 0 on raw line 80 (zxula_timing.vhd:167,441-451)",
          at(tpent, 117, 80).vc_ula == 0);
    check("RS-VCULA-04", "48K 60 Hz vc_ula = 0 on raw line 40 (zxula_timing.vhd:297,441-451)",
          at(t48_60, 117, 40).vc_ula == 0);
    check("RS-VCULA-05", "48K last paper line vc_ula = 191 on raw line 255 (zxula.vhd:414)",
          at(t48, 300, 255).vc_ula == 191);

    // vc_ula wraps on the machine's own frame length, `c_max_vc + 1`
    // (zxula_timing.vhd:204 / :270 / :168 / :298).  A raw line ABOVE
    // c_min_vactive is where that shows: the rebase goes negative and the
    // modulus is the only thing that decides the answer.
    check("RS-VCULA-06", "128K vc_ula above the frame top wraps on 311, not 312 (zxula_timing.vhd:203,204)",
          at(t128, 200, 10).vc_ula == 257);
    check("RS-VCULA-07", "+3 wraps on 311 like 128K (zxula_timing.vhd:203,204)",
          at(tp3, 200, 10).vc_ula == 257);
    check("RS-VCULA-08", "Pentagon wraps on 320 (zxula_timing.vhd:167,168)",
          at(tpent, 200, 10).vc_ula == 250);
    check("RS-VCULA-09", "48K 60 Hz wraps on 264 (zxula_timing.vhd:297,298)",
          at(t48_60, 200, 10).vc_ula == 234);
    check("RS-VCULA-10", "128K pre-origin line borrow wraps on 311 too (zxula_timing.vhd:204,441-451)",
          at(t128, 124, 64).vc_ula == 310);

    // cvc — the counter NR 0x1E/0x1F actually reports
    // (zxnext.vhd:5982-5986), reloaded from NR 0x64 at ula_min_vactive and
    // wrapped at c_max_vc (zxula_timing.vhd:455-470). GH #16 read it as the
    // raw frame line.
    {
        VideoTiming t = make_timing(MachineTimingMode::Timing48);
        check("RS-CVC-01", "cvc == vc_ula when NR 0x64 = 0 (zxula_timing.vhd:455-470)",
              at(t, 300, 100).cvc == at(t, 300, 100).vc_ula);
        check("RS-CVC-04", "cvc is NOT the raw frame line — NR 0x1E/0x1F is paper-relative (zxnext.vhd:5982-5986)",
              at(t, 300, 100).cvc == 36 && at(t, 300, 100).raw_vc == 100);
        t.set_cu_offset(10);
        check("RS-CVC-02", "NR 0x64 offset 10 shifts cvc by 10 (zxula_timing.vhd:462)",
              at(t, 300, 100).cvc == 46 && at(t, 300, 100).vc_ula == 36);
        t.set_cu_offset(200);
        check("RS-CVC-03", "cvc wraps at c_max_vc: vc_ula 191 + 200 = 79 mod 312 (zxula_timing.vhd:463-466)",
              at(t, 300, 255).cvc == 79);
    }
    // ...and the wrap is on the MACHINE's c_max_vc, not 48K's
    // (zxula_timing.vhd:463-466 against :204 / :168).
    {
        VideoTiming t = make_timing(MachineTimingMode::Timing128);
        t.set_cu_offset(200);
        check("RS-CVC-05", "128K cvc wraps on 311: vc_ula 191 + 200 = 80 (zxula_timing.vhd:204,463-466)",
              at(t, 300, 255).cvc == 80);
    }
    {
        VideoTiming t = make_timing(MachineTimingMode::TimingPentagon);
        t.set_cu_offset(200);
        check("RS-CVC-06", "Pentagon cvc wraps on 320: vc_ula 175 + 200 = 55 (zxula_timing.vhd:168,463-466)",
              at(t, 300, 255).cvc == 55);
    }

    // ── 3. Region classification ─────────────────────────────────────────

    check("RS-REG-01", "48K raw (129,100) is Paper — first displayed pixel (zxula.vhd:415)",
          at(t48, 129, 100).region == RasterRegion::Paper);
    check("RS-REG-02", "48K raw (128,100) is Border — c_min_hactive itself, one pixel early (zxula_timing.vhd:344)",
          at(t48, 128, 100).region == RasterRegion::Border);
    check("RS-REG-03", "48K raw (385,100) is Border — phc 256, i_phc(8) set (zxula.vhd:415)",
          at(t48, 385, 100).region == RasterRegion::Border);
    check("RS-REG-04", "48K raw hc 95 is Blanking — c_max_hblank inclusive (zxula_timing.vhd:260,351)",
          at(t48, 95, 100).region == RasterRegion::Blanking);
    check("RS-REG-05", "48K raw hc 96 is Border — one tick past c_max_hblank (zxula_timing.vhd:260,351)",
          at(t48, 96, 100).region == RasterRegion::Border);
    check("RS-REG-06", "48K raw vc 7 is Blanking at any hc — c_max_vblank inclusive (zxula_timing.vhd:268,351)",
          at(t48, 200, 7).region == RasterRegion::Blanking);
    check("RS-REG-07", "48K raw vc 8 is Border at raw hc 200 (zxula_timing.vhd:268,351)",
          at(t48, 200, 8).region == RasterRegion::Border);
    check("RS-REG-08", "48K raw vc 256 is Border — vc_ula 192, border_active_v (zxula.vhd:414)",
          at(t48, 300, 256).region == RasterRegion::Border);
    check("RS-REG-09", "Pentagon raw hc 80 is Border where 48K is Blanking (zxula_timing.vhd:158 vs :260)",
          at(tpent, 80, 100).region == RasterRegion::Border
       && at(t48,   80, 100).region == RasterRegion::Blanking);
    check("RS-REG-10", "Pentagon raw vc 10 is Blanking where 48K is Border (zxula_timing.vhd:166 vs :268)",
          at(tpent, 200, 10).region == RasterRegion::Blanking
       && at(t48,   200, 10).region == RasterRegion::Border);
    check("RS-REG-11", "128K Paper starts at raw hc 137 = c_min_hactive+1 (zxula_timing.vhd:195, zxula.vhd:415)",
          at(t128, 137, 100).region == RasterRegion::Paper
       && at(t128, 136, 100).region == RasterRegion::Border);
    check("RS-REG-12", "+3 Paper window matches 128K (zxula_timing.vhd:195)",
          at(tp3, 137, 100).region == RasterRegion::Paper
       && at(tp3, 136, 100).region == RasterRegion::Border);
    check("RS-REG-13", "Pentagon Paper starts on raw line 80, Border on 79 (zxula_timing.vhd:167)",
          at(tpent, 200, 80).region == RasterRegion::Paper
       && at(tpent, 200, 79).region == RasterRegion::Border);
    check("RS-REG-14", "48K Paper starts on raw line 64, Border on 63 (zxula_timing.vhd:269)",
          at(t48, 200, 64).region == RasterRegion::Paper
       && at(t48, 200, 63).region == RasterRegion::Border);
    check("RS-REG-15", "in_paper() agrees with region == Paper (zxula.vhd:415)",
          at(t48, 200, 100).in_paper() && !at(t48, 96, 100).in_paper());

    // ── 4. ULA fetch schedule ────────────────────────────────────────────
    // vram_a is reassigned on every odd i_hc(3:0) and holds through the
    // following even+odd pair, while vram_rd is asserted on odd ticks
    // (zxula.vhd:226-263); pixel addresses are set at F/3/7/B and attribute
    // addresses at 1/5/9/D, and the latch schedule at :270-303 pairs pbyte*
    // with i_hc(3:0) 1/5/9/D and abyte* with 3/7/B/F. Net: hc_ula bit 1
    // selects the class of the byte in flight.

    check("RS-FET-01", "48K hc_ula 0 fetches a bitmap byte (zxula.vhd:234-235,270-303)",
          at(t48, 117, 100).fetch == UlaFetch::Bitmap);
    check("RS-FET-02", "48K hc_ula 2 fetches an attribute byte (zxula.vhd:238-241,270-303)",
          at(t48, 119, 100).fetch == UlaFetch::Attribute);
    check("RS-FET-03", "48K hc_ula 4 fetches a bitmap byte (zxula.vhd:234-235)",
          at(t48, 121, 100).fetch == UlaFetch::Bitmap);
    check("RS-FET-04", "48K hc_ula 6 fetches an attribute byte (zxula.vhd:238-241)",
          at(t48, 123, 100).fetch == UlaFetch::Attribute);
    {
        // One whole 16-tick block: B B A A B B A A B B A A B B A A.
        bool ok = true;
        for (int q = 0; q < 16; ++q) {
            const UlaFetch want = ((q & 2) != 0) ? UlaFetch::Attribute
                                                 : UlaFetch::Bitmap;
            if (at(t48, 117 + 128 + q, 100).fetch != want) ok = false;
        }
        check("RS-FET-05", "48K whole 16-tick block is BBAA x4 (zxula.vhd:226-263,270-303)", ok);
    }
    check("RS-FET-06", "48K hc_ula 255 is the last fetching tick (zxula.vhd:416)",
          at(t48, 117 + 255, 100).fetch != UlaFetch::Idle);
    check("RS-FET-07", "48K hc_ula 256 is Idle — i_hc(8) sets border_active_ula (zxula.vhd:416)",
          at(t48, 117 + 256, 100).fetch == UlaFetch::Idle);
    check("RS-FET-08", "vc_ula 192 is Idle at every hc_ula — border_active_v (zxula.vhd:414,416)",
          at(t48, 117, 256).fetch == UlaFetch::Idle
       && at(t48, 200, 256).fetch == UlaFetch::Idle);
    check("RS-FET-09", "the ULA fetches while the beam is still in the LEFT BORDER (zxula.vhd:415 vs :416)",
          at(t48, 117, 100).region == RasterRegion::Border
       && at(t48, 117, 100).fetch  == UlaFetch::Bitmap);
    check("RS-FET-10", "the beam is still in PAPER after the fetch window closes (zxula.vhd:415 vs :416)",
          at(t48, 380, 100).region == RasterRegion::Paper
       && at(t48, 380, 100).fetch  == UlaFetch::Idle);
    check("RS-FET-11", "Timex hi-colour (port 0xFF=0x02): attribute slots fetch a 2nd bitmap plane (zxula.vhd:238-239)",
          at(t48, 119, 100, 0x02).fetch == UlaFetch::Bitmap);
    check("RS-FET-12", "Timex hi-res (port 0xFF=0x06): attribute slots fetch a 2nd bitmap plane (zxula.vhd:248-249)",
          at(t48, 119, 100, 0x06).fetch == UlaFetch::Bitmap);
    check("RS-FET-13", "shadow screen forces screen_mode \"000\" — attributes again (zxula.vhd:191)",
          at(t48, 119, 100, 0x02, true).fetch == UlaFetch::Attribute);
    check("RS-FET-14", "port 0xFF=0x01 (alternate screen) keeps attribute fetches (zxula.vhd:238-241)",
          at(t48, 119, 100, 0x01).fetch == UlaFetch::Attribute);
    check("RS-FET-15", "128K fetch window is raw hc [125,380] (zxula_timing.vhd:195,423, zxula.vhd:416)",
          at(t128, 125, 100).fetch != UlaFetch::Idle
       && at(t128, 380, 100).fetch != UlaFetch::Idle
       && at(t128, 381, 100).fetch == UlaFetch::Idle
       && at(t128, 124, 100).fetch == UlaFetch::Idle);
    check("RS-FET-16", "Pentagon fetch window is raw hc [117,372] on paper lines 80.. (zxula_timing.vhd:159,167)",
          at(tpent, 117, 80).fetch != UlaFetch::Idle
       && at(tpent, 372, 80).fetch != UlaFetch::Idle
       && at(tpent, 373, 80).fetch == UlaFetch::Idle
       && at(tpent, 200, 79).fetch == UlaFetch::Idle);
    check("RS-FET-17", "Timex mode bit 1 selects the plane; bit 0/2 alone do not (zxula.vhd:238-241,248-251)",
          at(t48, 119, 100, 0x04).fetch == UlaFetch::Attribute
       && at(t48, 119, 100, 0x03).fetch == UlaFetch::Bitmap);

    // ── 5. Display names (the panel and this suite share one wording) ────

    check("RS-NAME-01", "\"Paper\" names border_active = '0' (zxula.vhd:415)",
          std::string_view(raster_region_name(RasterRegion::Paper)) == "Paper");
    check("RS-NAME-02", "\"Border\" names border_active = '1' inside blank_n (zxula.vhd:415)",
          std::string_view(raster_region_name(RasterRegion::Border)) == "Border");
    check("RS-NAME-03", "\"Blanking\" names blank_n = '0' (zxula_timing.vhd:348-357)",
          std::string_view(raster_region_name(RasterRegion::Blanking)) == "Blanking");
    check("RS-NAME-04", "\"Bitmap\" names a pixel-byte read cycle (zxula.vhd:234-235)",
          std::string_view(ula_fetch_name(UlaFetch::Bitmap)) == "Bitmap");
    check("RS-NAME-05", "\"Attribute\" names an attribute-byte read cycle (zxula.vhd:240-241)",
          std::string_view(ula_fetch_name(UlaFetch::Attribute)) == "Attribute");
    check("RS-NAME-06", "\"Idle\" names border_active_ula = '1' (zxula.vhd:416)",
          std::string_view(ula_fetch_name(UlaFetch::Idle)) == "Idle");

    // ── 6. Out-of-range folding ──────────────────────────────────────────
    // The counters are hardware wrap-around counters; the debugger must fold
    // rather than fault if it is handed a stale position across a mid-frame
    // machine-timing change (Emulator::apply_video_timing re-inits at a frame
    // edge, zxnext.vhd:6694-6703).

    check("RS-FOLD-01", "raw hc past c_max_hc folds into the line (zxula_timing.vhd:316-327)",
          at(t48, 448 + 129, 100).region == RasterRegion::Paper
       && at(t48, 448 + 129, 100).raw_hc == 129);
    check("RS-FOLD-02", "negative raw vc folds into the frame (zxula_timing.vhd:329-341)",
          at(t48, 200, -1).raw_vc == 311);
    // Both frame counters wrap on the machine's own period — c_max_hc + 1 and
    // c_max_vc + 1 (zxula_timing.vhd:316-327 / :329-341, against the per-machine
    // constants at :160/:168, :196/:204, :262/:270, :290/:298).  48K alone
    // cannot see a modulus frozen at 448 / 312, because those ARE its values.
    check("RS-FOLD-03", "128K raw hc folds on 456, not 448 (zxula_timing.vhd:196,316-327)",
          at(t128, 456 + 137, 100).raw_hc == 137
       && at(t128, 456 + 137, 100).region == RasterRegion::Paper);
    check("RS-FOLD-04", "+3 raw hc folds on 456 (zxula_timing.vhd:196,316-327)",
          at(tp3, 456 + 137, 100).raw_hc == 137);
    check("RS-FOLD-05", "Pentagon raw hc folds on 448 (zxula_timing.vhd:160,316-327)",
          at(tpent, 448 + 129, 100).raw_hc == 129
       && at(tpent, 448 + 129, 100).region == RasterRegion::Paper);
    check("RS-FOLD-06", "128K negative raw vc folds on 311 (zxula_timing.vhd:204,329-341)",
          at(t128, 200, -1).raw_vc == 310);
    check("RS-FOLD-07", "Pentagon negative raw vc folds on 320 (zxula_timing.vhd:168,329-341)",
          at(tpent, 200, -1).raw_vc == 319);
    check("RS-FOLD-08", "48K 60 Hz negative raw vc folds on 264 (zxula_timing.vhd:298,329-341)",
          at(t48_60, 200, -1).raw_vc == 263);

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
