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
#include <QObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QString>
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

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
