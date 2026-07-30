// ===========================================================================
// The ESP status cell — how a GUI user finds out that a connection was made
// or refused. GH #25 branch 4, design doc §8.1 item 7.
//
// WHY THIS SUITE EXISTS. The AT engine already logs every connection opened,
// refused and closed, at info/warn, on by default. Those lines go to STDERR,
// which a GUI user never sees — the design doc records that as the branch's
// unmet obligation, and it is the case that matters most: someone launching a
// downloaded NEX from a desktop has no terminal at all. The status cell is the
// answer, so it is the thing that has to be tested, not the log line.
//
// AND IT IS TESTED FROM THE GUEST'S END. ESPUI-04..07 do not poke the
// connection log; they write an `AT+CIPSTART` into UART 0's TX FIFO exactly as
// a Z80 program would and run frames. That exercises the whole chain — guest
// TX -> UartChannel -> EspUartAdapter -> ThreadedEsp worker -> the allowlist ->
// the event log -> the status cell — which is the only version of this that is
// worth anything: every link in it is one this branch added.
//
// Qt is required (MainWindow is a real QWidget), no display is: main() forces
// the offscreen QPA platform, as preferences_apply_test and esc_break_test do.
// Run: ./build/test/esp_status_test
// ===========================================================================

#include <QApplication>
#include <QLabel>
#include <QStatusBar>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "gui/main_window.h"
#include "peripheral/esp_host_policy.h"
#include "peripheral/uart.h"
#include "platform/emulator_boot.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool ok, const std::string& detail = {}) {
    ++g_total;
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %-12s %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("[FAIL] %-12s %s%s%s\n", id, desc, detail.empty() ? "" : " -- ",
                    detail.c_str());
    }
}

/// The status-bar cell whose text starts with "ESP:", or null when the window
/// is not showing one. Found by content rather than by a friend declaration:
/// what the user sees is the assertion, and a cell the user cannot read is not
/// a satisfied requirement whatever the member pointer says.
QLabel* esp_cell(MainWindow& win) {
    for (QLabel* label : win.statusBar()->findChildren<QLabel*>()) {
        if (label->text().startsWith(QStringLiteral("ESP:")) && label->isVisibleTo(&win))
            return label;
    }
    return nullptr;
}

/// A window bound to a running machine. 48K is the cheapest to init and needs
/// no SD image; the ESP hangs off UART 0 regardless of machine type.
struct Fixture {
    Emulator   emu;
    MainWindow win;

    explicit Fixture(bool esp_enabled, std::vector<std::string> allow = {}) {
        EmulatorConfig cfg;
        cfg.type              = MachineType::ZX48K;
        cfg.silent            = true;
        cfg.esp_enabled       = esp_enabled;
        cfg.esp_allowed_hosts = std::move(allow);
        emu.init(cfg);
        win.set_emulator(&emu);
    }

    /// One status refresh, exactly as QtApp's 1 Hz status timer does it.
    void refresh() { win.update_status(50.0, 50.0, 0); }

    /// Send an AT line the way a Z80 program does: into UART 0's TX FIFO.
    void guest_sends(const std::string& line) {
        UartChannel& ch = emu.uart().channel(0);
        for (unsigned char c : line) ch.write_tx(c);
    }

    /// Run frames until the ESP has reported something, or give up. Bounded so
    /// a broken chain fails the row instead of hanging the suite.
    bool wait_for_event(int max_frames = 400) {
        for (int i = 0; i < max_frames; ++i) {
            emu.run_frame();
            const EspConnectionLog* log = emu.esp_events();
            if (log && log->sequence() > 0) return true;
            // The refusal happens on the ThreadedEsp worker, so give it a slot.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
};

void test_disabled() {
    Fixture f{false};
    f.refresh();
    check("ESPUI-01", "with the ESP off the status bar shows no ESP cell at all",
          esp_cell(f.win) == nullptr);
}

void test_enabled_idle() {
    {
        Fixture f{true};
        f.refresh();
        QLabel* cell = esp_cell(f.win);
        check("ESPUI-02", "with the ESP on the cell exists — its presence IS the indicator",
              cell != nullptr);
        check("ESPUI-03", "...and says the guest may reach any host when no allowlist is set",
              cell && cell->text().contains(QStringLiteral("any host")),
              cell ? cell->text().toStdString() : "no cell");
    }
    {
        Fixture f{true, {"nx.nxtel.org", "sync.lan"}};
        f.refresh();
        QLabel* cell = esp_cell(f.win);
        check("ESPUI-04", "...and reports how many hosts the allowlist admits",
              cell && cell->text().contains(QStringLiteral("2 allowed")),
              cell ? cell->text().toStdString() : "no cell");
        check("ESPUI-05", "...with the hosts themselves named in the tooltip",
              cell && cell->toolTip().contains(QStringLiteral("nx.nxtel.org")) &&
                  cell->toolTip().contains(QStringLiteral("sync.lan")),
              cell ? cell->toolTip().toStdString() : "no cell");
    }
}

void test_refusal_from_the_guest() {
    // The whole chain, driven from the Z80 side: a program asks for a host the
    // user did not allow, and the user must be able to see that it did.
    Fixture f{true, {"allowed.test"}};
    f.guest_sends("AT+CIPSTART=\"TCP\",\"evil.test\",80\r\n");
    const bool reported = f.wait_for_event();
    check("ESPUI-06", "a guest AT+CIPSTART to a disallowed host reaches the allowlist",
          reported);

    f.refresh();
    QLabel* cell = esp_cell(f.win);
    check("ESPUI-07", "...and the status cell names the refusal and the host it refused",
          cell && cell->text().contains(QStringLiteral("REFUSED")) &&
              cell->text().contains(QStringLiteral("evil.test:80")),
          cell ? cell->text().toStdString() : "no cell");
    check("ESPUI-08", "...in red, so it is not mistaken for ordinary status text",
          cell && cell->styleSheet().contains(QStringLiteral("red")),
          cell ? cell->styleSheet().toStdString() : "no cell");
    check("ESPUI-09", "...and the tooltip keeps the history, which the cell cannot show",
          cell && cell->toolTip().contains(QStringLiteral("refused")) &&
              cell->toolTip().contains(QStringLiteral("evil.test")),
          cell ? cell->toolTip().toStdString() : "no cell");
}

void test_cold_boot_resets_the_cell() {
    // A cold boot builds a FRESH connection log whose sequence restarts at 0,
    // and the replacement routinely lands on the block the old one was freed
    // from — so an observer that cached only the sequence can mistake "N events
    // into the new machine" for "the N events I already rendered" and keep
    // showing pre-reboot text. That is why the change test is the
    // (instance id, sequence) PAIR.
    //
    // The row forces the exact coincidence rather than hoping for it: it
    // renders a refusal, reboots, and drives the new machine to the SAME
    // sequence number before refreshing again.
    Fixture f{true, {"allowed.test"}};
    f.guest_sends("AT+CIPSTART=\"TCP\",\"evil.test\",80\r\n");
    const bool first = f.wait_for_event();
    f.refresh();
    QLabel* cell = esp_cell(f.win);
    check("ESPUI-10", "the pre-reboot refusal is on screen",
          first && cell && cell->text().contains(QStringLiteral("REFUSED")),
          cell ? cell->text().toStdString() : "no cell");

    EmulatorConfig cfg;
    cfg.type              = MachineType::ZX48K;
    cfg.silent            = true;
    cfg.esp_enabled       = true;
    cfg.esp_allowed_hosts = {"allowed.test"};
    emulator_cold_boot(f.emu, cfg);

    // One event into the fresh machine — the same sequence value the stale
    // cell was already showing.
    f.guest_sends("AT+CIPSTART=\"TCP\",\"other.test\",81\r\n");
    const bool second = f.wait_for_event();
    f.refresh();
    cell = esp_cell(f.win);
    check("ESPUI-11", "the post-reboot refusal reaches the cell at the same sequence",
          second && cell && cell->text().contains(QStringLiteral("other.test:81")),
          cell ? cell->text().toStdString() : "no cell");
    check("ESPUI-12", "...so no pre-reboot text survives the boot",
          cell && !cell->text().contains(QStringLiteral("evil.test")),
          cell ? cell->text().toStdString() : "no cell");
}

void test_address_policy_refusal_from_the_guest() {
    // THE OTHER HALF OF THE SAME SECURITY CONTROL (GH #161). The allowlist is
    // enforced in jnext (`EspGatedTransport::begin_connect`); the ADDRESS policy
    // is enforced inside the reusable transport, which has no vocabulary richer
    // than `Failed` to report with. So a guest reaching for 127.0.0.1 — or a
    // cloud-metadata endpoint — used to produce a cell indistinguishable from a
    // DNS miss or a timeout: the block happened, but it did not look deliberate.
    //
    // No allowlist here on purpose: the NAME passes, and the refusal can only
    // come from the address policy the transport applies after resolving it.
    // 127.0.0.1 is a literal, so no DNS is contacted and no socket is opened —
    // the row is hermetic.
    Fixture f{true};
    f.guest_sends("AT+CIPSTART=\"TCP\",\"127.0.0.1\",8080\r\n");
    const bool reported = f.wait_for_event();
    f.refresh();
    QLabel* cell = esp_cell(f.win);
    const std::string why =
        !reported ? "the guest AT+CIPSTART never reached the ESP"
                  : (cell ? cell->text().toStdString() : "no cell");

    check("ESPUI-13", "a policy-blocked address reaches the cell as a REFUSAL, not a failure",
          reported && cell && cell->text().contains(QStringLiteral("REFUSED")) &&
              cell->text().contains(QStringLiteral("127.0.0.1:8080")),
          why);
    check("ESPUI-14", "...in red, exactly like an allowlist refusal",
          cell && cell->styleSheet().contains(QStringLiteral("red")),
          cell ? cell->styleSheet().toStdString() : "no cell");
    // The tooltip carries the HISTORY, and it has to classify each entry too —
    // the reason text alone was always there, printed under a "failed" heading,
    // which is precisely the thing that made a deliberate block unreadable.
    check("ESPUI-15", "...and the history entry is filed as refused, not as failed",
          cell &&
              cell->toolTip().contains(QStringLiteral("refused 127.0.0.1:8080")) &&
              !cell->toolTip().contains(QStringLiteral("failed 127.0.0.1:8080")) &&
              cell->toolTip().contains(QStringLiteral("address refused by policy")),
          cell ? cell->toolTip().toStdString() : "no cell");
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("=== ESP-01 status cell (GH #25 branch 4) ===\n\n");

    test_disabled();
    test_enabled_idle();
    test_refusal_from_the_guest();
    test_address_policy_refusal_from_the_guest();
    test_cold_boot_resets_the_cell();

    std::printf("\n============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
