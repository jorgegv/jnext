// GH #358 point 2 - the download dialog through a dropped network, driven for real.
//
// Oracle: the tick / stall / cancel contract in core/sdcard_provisioner.h, which
// is jnext policy (owner, #358 comment of 2026-10-17: "make the hung connection
// more responsive than 1-2 minutes, or at least give some feedback to the user
// (e.g. 'stalled...')"). There is no VHDL counterpart: this is host-side GUI.
//
// WHAT WENT WRONG. janko-jj pulled the network cable in the middle of a NextPi
// download; the window became "Not Responding" for a minute or two and said
// nothing until the network came back. The download runs on the GUI thread
// (before QtApp exists), so the window lives exactly as often as the backend
// calls SdcardGuiProvisioner::progress(), which pumps the event loop. The
// backends now call it at least every 200 ms whatever the network does, and the
// dialog says "Stalled: no data received for N s" once no byte has arrived for
// 5 s. These rows pin the dialog's half on a clock the test owns (SDD-01, SDD-02),
// the composition with a real backend, a real silent server and the PRODUCTION
// clock - the pump keeps running through the stall and the dialog really says
// "Stalled" (SDD-03) - and that a failed SD-card download is reported to a GUI
// user instead of the window silently vanishing (SDD-04).
//
// GH #385 adds SDD-05..09. The warning dialog's text is selectable by mouse whatever the widget style,
// so the address in a failure message can be copied (SDD-05; owner, #385: "so it can be checked by other
// means"). The short fetches of the NextPi provisioner (the release list, the checksums) get feedback
// only when one is slow - nothing at all for the first second (SDD-06), then the download dialog with
// its stall notice and Cancel (SDD-07), a Cancel that ends only that fetch (SDD-08) - and the pump keeps
// the session alive through a real, silent backend on the production clock (SDD-09). The oracle is the
// ShortFetchFn contract in core/sdcard_provisioner.h and SdcardGuiProvisioner::short_fetch (jnext
// policy: the #385 ruling asks for the existing progress dialog and stall notice on those fetches).
//
// WHAT THIS CANNOT SHOW. Windows' own hung-window detection (IsHungAppWindow,
// five seconds without a PeekMessage) and the Qt dialog on top of WinHTTP: this
// suite runs on Linux only (offscreen QPA), where the backend is libcurl. The
// WinHTTP cadence is pinned by PROV-STALL-01..04/08 under wine; the two meet in
// SdcardGuiProvisioner::progress(), shared by every platform. SDD-04 checks the
// WIRING of the failure warning, not the dialog: SdcardGuiProvisioner::warn()
// returns at once on the offscreen platform, so no modal appears to be seen.
//
// Run: ./build/test/sdcard_dialog_test

#include "core/sdcard_provisioner.h"
#include "gui/sdcard_download_dialog.h"
#include "../sdcard_provisioner/range_server.h"

#include <QApplication>
#include <QByteArray>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Clk = std::chrono::steady_clock;

double since(Clk::time_point t0) { return std::chrono::duration<double>(Clk::now() - t0).count(); }

// The one QProgressDialog on screen (the helper owns it), or null.
QProgressDialog* the_dialog() {
    for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* d = qobject_cast<QProgressDialog*>(w)) return d;
    return nullptr;
}

std::string label() {
    QProgressDialog* d = the_dialog();
    return d ? d->labelText().toStdString() : std::string("<no dialog>");
}

bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// The dialog's Cancel button, clicked the way a user does.
bool click_cancel() {
    QProgressDialog* d = the_dialog();
    QPushButton* b = d ? d->findChild<QPushButton*>() : nullptr;
    if (!b) return false;
    b->click();
    return true;
}

std::string code_of(const char* path) {   // a source file, line comments removed
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string code, line;
    std::istringstream ls(ss.str());
    while (std::getline(ls, line)) {
        const size_t c = line.find("//");
        code += (c == std::string::npos ? line : line.substr(0, c)) + "\n";
    }
    return code;
}

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// -- SDD-01 / SDD-02: the dialog's notice, on a clock the test owns ---------

void test_notice_and_cancel() {
    using namespace std::chrono;
    const Clk::time_point base = Clk::now();
    Clk::duration offset = Clk::duration::zero();
    SdcardGuiProvisioner gui([&] { return base + offset; });
    const std::string plain = "Downloading the test image";
    gui.set_texts("jnext - test", "headline", "busy", plain);
    auto advance_ms = [&](int ms) { offset += milliseconds(ms); };

    // The confirm box before the first call can sit open for a minute: not a stall.
    advance_ms(60000);
    gui.progress(0, 300000);
    const std::string first = label();
    advance_ms(4900);
    gui.progress(0, 300000);
    const std::string at_4_9 = label();
    advance_ms(100);
    gui.progress(0, 300000);
    const std::string at_5_0 = label();
    advance_ms(7400);
    gui.progress(0, 300000);
    const std::string at_12_4 = label();
    advance_ms(200);
    gui.progress(1000, 300000);
    const std::string after_bytes = label();
    advance_ms(6000);
    gui.progress(1000, 300000);
    const std::string at_6_again = label();

    check("SDD-01",
          "the dialog's label is the plain one after a 60 s confirm and until 5 s with no byte; at 5.0 s it says "
          "'Stalled: no data received for 5 s' and 'gives up after 30 s'; at 12.4 s '12 s'; when bytes arrive "
          "it is the plain label again; 6 s later the count restarted at 6 s",
          first == plain && at_4_9 == plain && has(at_5_0, "Stalled: no data received for 5 s") &&
              has(at_5_0, "gives up after 30 s") && at_5_0.compare(0, plain.size(), plain) == 0 &&
              has(at_12_4, "Stalled: no data received for 12 s") && after_bytes == plain &&
              has(at_6_again, "Stalled: no data received for 6 s"),
          "first='" + first.substr(0, 60) + "' 4.9='" + at_4_9.substr(0, 60) + "' 5.0='" + at_5_0 + "' 12.4='" +
              at_12_4.substr(0, 90) + "' bytes='" + after_bytes.substr(0, 60) + "' +6='" + at_6_again.substr(0, 90) + "'");

    // The note is up (SDD-01 ended on it). Cancel: this call and every later one say false.
    const bool clicked = click_cancel();
    const bool first_false = !gui.progress(1000, 300000);
    const bool second_false = !gui.progress(1000, 300000);
    check("SDD-02",
          "with the stall note showing, a click on the dialog's Cancel button makes the next progress() return "
          "false, and so does the one after (the download then ends 'cancelled by user')",
          clicked && first_false && second_false,
          "clicked=" + std::to_string(clicked) + " first_false=" + std::to_string(first_false) +
              " second_false=" + std::to_string(second_false));
}

// -- SDD-03: the real backend, a real silent server, the helper exactly as main.cpp wires it -

void test_through_the_backend() {
    rs::Server srv;
    if (!srv.ok()) {
        check("SDD-03", "loopback server fixture started", false, "cannot listen on 127.0.0.1");
        return;
    }
    const std::string body = rs::pattern(300000);
    rs::Opts o;
    o.body = body;
    o.etag = "\"v1\"";
    o.stall_after = 100000;   // 100000 of 300000 bytes, then silence
    o.stall_ms = 20000;       // the click below ends it long before
    srv.set(o);

    const std::string dir = jtp::make_temp_dir("jnext_sdd_");
    const std::string dest = dir + "/dest.bin";

    SdcardGuiProvisioner gui;   // the real clock, as main.cpp builds it
    // main.cpp: opts.progress = [&](uint64_t d, uint64_t t) { return gui.progress(d, t); };
    const Clk::time_point t0 = Clk::now();
    double first_at = -1, click_at = -1;
    bool visible = false;
    std::string label_at_click;
    std::vector<double> fires;
    QTimer ticker;   // an application timer: it only fires if the event loop is pumped
    QObject::connect(&ticker, &QTimer::timeout, [&] { fires.push_back(since(t0)); });
    ticker.start(50);
    // The user gives up 6.5 s into the stall: past the 5 s the notice needs, well short of the 10 s the
    // backend below waits. What the label says at that moment is what they were reading.
    QTimer click;
    click.setSingleShot(true);
    QObject::connect(&click, &QTimer::timeout, [&] {
        click_at = since(t0);
        label_at_click = label();
        click_cancel();
    });
    sdcard::ProgressFn pf = [&](uint64_t d, uint64_t t) {
        const bool go = gui.progress(d, t);
        if (d == 100000 && first_at < 0) {   // the last byte has arrived: the stall begins
            first_at = since(t0);
            QProgressDialog* dlg = the_dialog();
            visible = dlg && dlg->isVisible();
            click.start(6500);
        }
        return go;
    };
    std::string err;
    const bool ok = sdcard::http_download(srv.url(), dest, pf, err, {200, 10000});
    const double returned_at = since(t0);
    ticker.stop();

    int n = 0;
    double prev = -1, gap = 0;
    for (double f : fires) {
        if (first_at < 0 || f < first_at || (click_at >= 0 && f > click_at)) continue;
        ++n;
        if (prev >= 0) gap = std::max(gap, f - prev);
        prev = f;
    }
    sdcard::ResumeInfo ri;
    // The production clock's notice: "Stalled: no data received for N s", N counted from the last byte.
    const std::string key = "Stalled: no data received for ";
    const size_t key_at = label_at_click.find(key);
    const int shown_s = key_at == std::string::npos ? -1 : std::atoi(label_at_click.c_str() + key_at + key.size());
    check("SDD-03",
          "through the real backend and a server that goes silent after 100000 of 300000 bytes, with the helper "
          "wired as main.cpp wires it (the real clock): the dialog is visible, an application timer keeps firing "
          "(at least 5 times, never 800 ms apart) between the first call at 100000 bytes and the click on Cancel "
          "6.5 s later, the label the user was reading at the click says 'Stalled: no data received for N s' "
          "with N from 5 to 9 and 'gives up after 30 s', and the call returns within 1 s of the click saying "
          "'cancelled by user' with the 100000 bytes kept",
          !ok && visible && first_at >= 0 && click_at > first_at && n >= 5 && gap <= 0.8 &&
              shown_s >= 5 && shown_s <= 9 && has(label_at_click, "gives up after 30 s") &&
              returned_at - click_at <= 1.0 && err == "cancelled by user" &&
              slurp(sdcard::partial_path(dest)) == body.substr(0, 100000) && sdcard::read_resume_info(dest, ri),
          "ok=" + std::to_string(ok) + " visible=" + std::to_string(visible) + " first_at=" + std::to_string(first_at) +
              " click_at=" + std::to_string(click_at) + " fires=" + std::to_string(n) + " max_gap=" + std::to_string(gap) +
              " label_at_click='" + label_at_click + "' shown_s=" + std::to_string(shown_s) +
              " returned_after_click=" + std::to_string(returned_at - click_at) + " err='" + err + "' part=" +
              std::to_string(slurp(sdcard::partial_path(dest)).size()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// -- SDD-04: a failed SD-card download is reported to a GUI user (wiring) ---

void test_failure_is_reported() {
    const std::string code = code_of(JNEXT_MAIN_CPP);
    const size_t def = code.find("const bool sd_gui_dialogs = !headless && sdcard_file_add_count == 0;");
    const size_t run = code.find("sdcard::provision_sd_card(opts);");
    const size_t cond = code.find("if (sd_gui_dialogs && res.status == sdcard::ProvisionStatus::Failed", run);
    const size_t logged = code.find("Log::emulator()->error(\"SD-card image: {}\", res.error);", cond);
    const size_t warn = code.find("gui_prov.warn(", cond);
    const size_t ret = code.find("return 1;", run);
    check("SDD-04",
          "src/main.cpp: after provision_sd_card(opts) a FAILED (not declined) SD-card result in the GUI "
          "(!headless, no --sdcard-file-add) is logged and calls gui_prov.warn( before the 'return 1;' that exits "
          "(source check: it pins the wiring, not the dialog, because warn() returns at once on the offscreen "
          "platform)",
          def != std::string::npos && run != std::string::npos && cond != std::string::npos &&
              logged != std::string::npos && warn != std::string::npos && ret != std::string::npos && def < run &&
              run < cond && cond < logged && logged < warn && warn < ret);
}

// -- SDD-05: the warning dialog's text is selectable by mouse, whatever the style ---

void test_warning_text_selectable() {
    QStyle* const original = QApplication::style();
    const QString original_name = original ? original->objectName() : QString();
    // The "Windows" style's message-box default is links only (measured on Qt 6.11.2): the address in a
    // failure message could not be selected there. The control below is a plain QMessageBox under the same
    // style, so a box that is selectable only because the style says so cannot pass.
    QStyle* const windows = QApplication::setStyle(QStringLiteral("Windows"));
    const std::string msg = "The SD-card image could not be installed:\n\ndownload failed: x\n  URL: http://127.0.0.1:1/z.zip";
    QMessageBox control(QMessageBox::Warning, QStringLiteral("t"), QStringLiteral("m"), QMessageBox::Ok);
    const bool control_lacks = !(control.textInteractionFlags() & Qt::TextSelectableByMouse);
    const std::unique_ptr<QMessageBox> box = SdcardGuiProvisioner::warning_box("jnext - test", msg);
    const bool selectable = box && (box->textInteractionFlags() & Qt::TextSelectableByMouse);
    const bool same = box && box->text() == QString::fromStdString(msg) && box->icon() == QMessageBox::Warning &&
                      box->windowTitle() == QStringLiteral("jnext - test");
    QApplication::setStyle(original_name.isEmpty() ? QStringLiteral("Fusion") : original_name);
    check("SDD-05",
          "under the 'Windows' widget style (whose message-box default is links only, the control row) the "
          "warning dialog warn() runs has the text it was given, the Warning icon, its title, and its text "
          "selectable by mouse, so the address in it can be copied",
          windows != nullptr && control_lacks && selectable && same,
          "style=" + std::to_string(windows != nullptr) + " control_lacks=" + std::to_string(control_lacks) +
              " selectable=" + std::to_string(selectable) + " same=" + std::to_string(same));
}

// -- SDD-06..08: the short-fetch dialog on a clock the test owns -------------------

const std::string kShortLabel = "Reading the list of NextPi releases...";

void test_short_fetch_no_flash() {
    using namespace std::chrono;
    const Clk::time_point base = Clk::now();
    Clk::duration offset = Clk::duration::zero();
    SdcardGuiProvisioner gui([&] { return base + offset; });
    bool timer_fired = false;
    QTimer::singleShot(0, [&] { timer_fired = true; });   // fires only if the event loop is pumped
    bool nonempty = false, fired_in_first_call = false, dialog_ever = false, all_true = true;
    const bool ret = gui.short_fetch(kShortLabel, [&](const sdcard::ProgressFn& pf) {
        nonempty = static_cast<bool>(pf);
        bool first = true;
        for (int ms : {0, 200, 400, 600, 800, 900}) {
            offset = milliseconds(ms);
            all_true = (pf ? pf(0, 0) : false) && all_true;   // an empty ProgressFn fails the row, it does not abort
            if (first) fired_in_first_call = timer_fired;
            first = false;
            if (the_dialog()) dialog_ever = true;
        }
        return true;
    });
    check("SDD-06",
          "a short fetch that calls its ProgressFn six times in the first 900 ms (fake clock) shows no dialog at "
          "any call or after the return, hands the fetch a non-empty ProgressFn whose first call already pumps the "
          "event loop (a zero-time timer posted before fires inside it), and returns what the fetch returned",
          nonempty && fired_in_first_call && !dialog_ever && !the_dialog() && all_true && ret,
          "nonempty=" + std::to_string(nonempty) + " timer_in_first_call=" + std::to_string(fired_in_first_call) +
              " dialog_ever=" + std::to_string(dialog_ever) + " ret=" + std::to_string(ret));
}

void test_short_fetch_dialog_and_stall() {
    using namespace std::chrono;
    const Clk::time_point base = Clk::now();
    Clk::duration offset = Clk::duration::zero();
    SdcardGuiProvisioner gui([&] { return base + offset; });
    struct Snap { int ms; bool exists, visible, modal; std::string title, text; };
    std::vector<Snap> snaps;
    const bool ret = gui.short_fetch(kShortLabel, [&](const sdcard::ProgressFn& pf) {
        for (int ms = 0; ms <= 6000; ms += 200) {
            offset = milliseconds(ms);
            if (pf) pf(0, 0);   // no byte, ever
            QProgressDialog* d = the_dialog();
            snaps.push_back({ms, d != nullptr, d && d->isVisible(), d && d->isModal(),
                             d ? d->windowTitle().toStdString() : std::string(),
                             d ? d->labelText().toStdString() : std::string()});
        }
        return false;
    });
    auto at = [&](int ms) -> const Snap& { return snaps.at(static_cast<std::size_t>(ms / 200)); };
    bool quiet_first_second = true, plain_until_5s = true;
    for (const Snap& sn : snaps) {
        if (sn.ms < 1000 && sn.exists) quiet_first_second = false;
        if (sn.ms >= 1000 && sn.ms < 5000 && !(sn.visible && sn.modal && sn.title == "jnext - Downloading" && sn.text == kShortLabel))
            plain_until_5s = false;
    }
    const std::string stalled = kShortLabel + "\n\n";
    const bool note_at_5 = has(at(5000).text, "Stalled: no data received for 5 s") && has(at(5000).text, "gives up after 30 s") &&
                           at(5000).text.compare(0, stalled.size(), stalled) == 0;
    const bool note_at_5_8 = has(at(5800).text, "Stalled: no data received for 5 s");
    const bool note_at_6 = has(at(6000).text, "Stalled: no data received for 6 s");
    check("SDD-07",
          "a short fetch that gets no byte (fake clock, a call every 200 ms for 6 s) shows nothing before 1000 ms; "
          "from the 1000 ms call a visible modal dialog titled 'jnext - Downloading' whose label is exactly the "
          "fetch's label until 5000 ms; from 5000 ms the label plus a blank line and 'Stalled: no data received for "
          "5 s' with 'gives up after 30 s' (still '5 s' at 5800 ms, '6 s' at 6000 ms); and when the fetch returns "
          "false so does short_fetch and no dialog is left",
          quiet_first_second && plain_until_5s && note_at_5 && note_at_5_8 && note_at_6 && !ret && !the_dialog(),
          "quiet=" + std::to_string(quiet_first_second) + " plain=" + std::to_string(plain_until_5s) + " 5.0='" +
              at(5000).text + "' 5.8='" + at(5800).text.substr(0, 90) + "' ret=" + std::to_string(ret) +
              " left=" + std::to_string(the_dialog() != nullptr));
}

void test_short_fetch_cancel_is_local() {
    using namespace std::chrono;
    const Clk::time_point base = Clk::now();
    Clk::duration offset = Clk::duration::zero();
    SdcardGuiProvisioner gui([&] { return base + offset; });
    const std::string archive_label = "Downloading the archive";
    gui.set_texts("jnext - test", "headline", "busy", archive_label);
    bool clicked = false, still_true_before = true, next_false = false;
    const bool ret = gui.short_fetch("Fetching the NextPi checksum...", [&](const sdcard::ProgressFn& pf) {
        for (int ms = 0; ms <= 2000; ms += 200) {
            offset = milliseconds(ms);
            still_true_before = (pf ? pf(0, 0) : false) && still_true_before;
        }
        clicked = click_cancel();   // 2000 ms: the user gives up on the checksum
        offset = milliseconds(2200);
        next_false = pf ? !pf(0, 0) : false;
        return !next_false;         // the backend would end with "cancelled by user"
    });
    // The checksum is over; the archive download goes on to its own dialog, and the click does not reach it.
    offset = milliseconds(2400);
    const bool archive_goes_on = gui.progress(0, 100);
    const std::string archive_text = label();
    check("SDD-08",
          "a click on Cancel in the short fetch's dialog makes the next call of its ProgressFn return false and "
          "short_fetch return what the fetch returned (false); it does not cancel the archive download that "
          "follows: progress() then returns true and its dialog says the download label, not the short fetch's",
          clicked && still_true_before && next_false && !ret && archive_goes_on && archive_text == archive_label,
          "clicked=" + std::to_string(clicked) + " before=" + std::to_string(still_true_before) + " next_false=" +
              std::to_string(next_false) + " ret=" + std::to_string(ret) + " archive_goes_on=" +
              std::to_string(archive_goes_on) + " label='" + archive_text + "'");
}

// -- SDD-09: the real backend, a real silent server, the production clock ----------

void test_short_fetch_through_the_backend() {
    rs::Server srv;
    if (!srv.ok()) {
        check("SDD-09", "loopback server fixture started", false, "cannot listen on 127.0.0.1");
        return;
    }
    rs::Opts o;
    o.silent = true;     // read the request, send nothing, hold the connection open
    o.stall_ms = 20000;  // the backend gives up at 2 s and closes: the hold ends early
    srv.set(o);

    const std::string dir = jtp::make_temp_dir("jnext_sdd9_");
    SdcardGuiProvisioner gui;   // the production clock
    const Clk::time_point t0 = Clk::now();
    std::vector<double> fires;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&] { fires.push_back(since(t0)); });
    ticker.start(50);
    int calls = 0;
    double first_visible = -1, last_before_1s_visible = -1;
    sdcard::ProgressFn spy;   // the backend gets this one: the gui's ProgressFn, then what the dialog looks like
    std::string err;
    const bool ok = gui.short_fetch(kShortLabel, [&](const sdcard::ProgressFn& pf) {
        spy = [&](uint64_t d, uint64_t t) {
            const bool go = pf ? pf(d, t) : true;   // an empty ProgressFn fails the row (no dialog), it does not abort
            ++calls;
            const double at = since(t0);
            QProgressDialog* dlg = the_dialog();
            const bool visible = dlg && dlg->isVisible();
            if (visible && first_visible < 0) first_visible = at;
            if (visible && at < 1.0) last_before_1s_visible = at;
            return go;
        };
        return sdcard::http_download(srv.url(), dir + "/dest.bin", spy, err, {200, 2000});
    });
    const double returned_at = since(t0);
    ticker.stop();
    int n = 0;
    double prev = -1, gap = 0;
    for (double f : fires) {
        if (f > returned_at) continue;
        ++n;
        if (prev >= 0) gap = std::max(gap, f - prev);
        prev = f;
    }
    check("SDD-09",
          "through the real backend and a server that never answers, with the production clock and a 2 s stall "
          "timeout: the fetch returns false after 1.9 to 5.0 s with 'stalled: no data received for 2 s', the "
          "dialog is visible at a ProgressFn call 1.0 s or more after the start and at none before it, an "
          "application timer keeps firing (at least 5 times, never 800 ms apart) during the fetch, and no dialog "
          "is left",
          !ok && returned_at >= 1.9 && returned_at <= 5.0 && err.rfind("stalled: no data received for 2 s", 0) == 0 &&
              first_visible >= 1.0 && last_before_1s_visible < 0 && n >= 5 && gap <= 0.8 && !the_dialog(),
          "ok=" + std::to_string(ok) + " returned_at=" + std::to_string(returned_at) + " err='" + err +
              "' first_visible=" + std::to_string(first_visible) + " visible_before_1s=" +
              std::to_string(last_before_1s_visible) + " calls=" + std::to_string(calls) + " fires=" +
              std::to_string(n) + " max_gap=" + std::to_string(gap) + " left=" + std::to_string(the_dialog() != nullptr));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

}  // namespace

int main(int argc, char** argv) {
    // The helper builds its dialogs on a QApplication it finds; give it one, on
    // the offscreen platform (no display needed).
    qputenv("QT_QPA_PLATFORM", "offscreen");
    jtp::set_env("no_proxy", "127.0.0.1,localhost");   // curl reads it; keeps a proxy out of loopback
    jtp::set_env("NO_PROXY", "127.0.0.1,localhost");
    QApplication app(argc, argv);

    std::printf("SD-card / NextPi download dialog tests (GH #358)\n\n");

    test_notice_and_cancel();
    test_through_the_backend();
    test_failure_is_reported();
    test_warning_text_selectable();
    test_short_fetch_no_flash();
    test_short_fetch_dialog_and_stall();
    test_short_fetch_cancel_is_local();
    test_short_fetch_through_the_backend();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
