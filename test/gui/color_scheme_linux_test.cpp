// ===========================================================================
// GH #363 / GH #414 — the private D-Bus daemon of color_scheme_test dies with
// the suite. Linux only (os: linux): the guard's kill-on-EOF and the /proc
// check for a dead daemon are Linux mechanisms, so the rows live in this
// sibling suite instead of failing in color_scheme_test on another OS.
//
// No VHDL oracle: host test infrastructure.
// CS-28: a suite SIGKILLed by the harness takes its dbus-daemon with it.
// CS-29: and a suite that exits normally takes it at once.
//
// Run: ./build/test/color_scheme_linux_test
// ===========================================================================
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <csignal>
#include <cstdio>
#include <string>
#include <sys/types.h>

#include "../row_id.h"
#include "private_bus.h"

namespace {

using private_bus::PrivateBus;
using private_bus::g_self;

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
    }
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}

// Is `pid` gone: no such process, or a zombie no one has reaped yet (a
// container's PID 1 may never reap it; it is dead either way).
bool process_gone(qint64 pid) {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) return true;
    const QByteArray line = stat.readAll();
    const int close = line.lastIndexOf(')');
    return close < 0 || line.mid(close + 2, 1) == "Z";
}

// CS-28 — a suite SIGKILLed by the harness (a timeout) takes its private
// dbus-daemon with it. A copy of this binary, run as --bus-child FILE, starts
// the bus and writes the daemon's pid to FILE; it is then SIGKILLed.
void test_bus_cleanup() {
    QTemporaryDir dir;
    const QString pidfile = dir.filePath("daemon.pid");
    QProcess child;
    QProcessEnvironment env;
    env.insert("LANG", "C");
    env.insert("PATH", qEnvironmentVariable("PATH"));
    child.setProcessEnvironment(env);
    child.start(QString::fromLocal8Bit(g_self), {"--bus-child", pidfile});
    qint64 daemon = 0;
    QElapsedTimer t;
    t.start();
    do {   // at least one poll, however late this thread is first scheduled (GH #399)
        if (!dir.isValid() || !child.waitForStarted(5000)) break;
        QFile f(pidfile);
        if (f.open(QIODevice::ReadOnly)) daemon = f.readAll().trimmed().toLongLong();
        if (daemon <= 0) QThread::msleep(20);
    } while (t.elapsed() < 10000 && daemon <= 0);
    const bool alive_before = daemon > 0 && !process_gone(daemon);
    child.kill();                                   // SIGKILL: no destructor runs
    child.waitForFinished(5000);
    bool gone = false;
    t.restart();
    while (daemon > 0 && !(gone = process_gone(daemon)) && t.elapsed() < 5000) QThread::msleep(20);
    if (daemon > 0 && !gone) ::kill(static_cast<pid_t>(daemon), SIGKILL);   // never leave it behind
    check("CS-28", "a suite SIGKILLed by the harness takes its private dbus-daemon with it (the bus "
                   "guard sees its stdin close), within 5 s",
          alive_before && gone,
          daemon > 0 ? "daemon " + std::to_string(daemon) + (alive_before ? " was alive" : " not alive") +
                           (gone ? ", gone after the kill" : ", STILL RUNNING after the kill")
                     : std::string("the child reported no daemon"));
}

// CS-29 — the normal end: a suite that EXITS takes its daemon with it, at
// once (~PrivateBus closes the guard's stdin). A --bus-child-exit copy starts
// the bus, writes the daemon's pid and returns from main().
void test_bus_normal_exit() {
    QTemporaryDir dir;
    const QString pidfile = dir.filePath("daemon.pid");
    QProcess child;
    QProcessEnvironment env;
    env.insert("LANG", "C");
    env.insert("PATH", qEnvironmentVariable("PATH"));
    child.setProcessEnvironment(env);
    QElapsedTimer t;
    t.start();
    child.start(QString::fromLocal8Bit(g_self), {"--bus-child-exit", pidfile});
    const bool exited = child.waitForFinished(15000);
    const qint64 ms = t.elapsed();
    if (!exited) { child.kill(); child.waitForFinished(5000); }   // SIGKILL past the bound
    QFile f(pidfile);
    const qint64 daemon = f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toLongLong() : 0;
    bool gone = false;
    t.restart();
    while (daemon > 0 && !(gone = process_gone(daemon)) && t.elapsed() < 2000) QThread::msleep(20);
    if (daemon > 0 && !gone) ::kill(static_cast<pid_t>(daemon), SIGKILL);   // never leave it behind
    check("CS-29", "a suite that exits normally takes its private dbus-daemon with it (the destructor "
                   "closes the guard's stdin), with no wait: exit within 3 s, daemon gone within 2 s",
          exited && child.exitCode() == 0 && ms < 3000 && daemon > 0 && gone,
          daemon > 0 ? "exit after " + std::to_string(ms) + " ms (code " + std::to_string(child.exitCode()) +
                           "), daemon " + std::to_string(daemon) + (gone ? " gone" : " STILL RUNNING")
                     : std::string("the child reported no daemon"));
}

// --bus-child FILE: start the private bus, publish the daemon's pid, then
// wait to be killed (bounded: 60 s); with `stay` false, return at once, so
// the caller's exit runs ~PrivateBus.
int bus_child(const char* pidfile, bool stay) {
    PrivateBus bus;
    QFile tmp(QString::fromLocal8Bit(pidfile) + ".tmp");
    if (bus.daemon_pid <= 0 || !tmp.open(QIODevice::WriteOnly)) return 1;
    tmp.write(QByteArray::number(bus.daemon_pid));
    tmp.close();
    if (!tmp.rename(QString::fromLocal8Bit(pidfile))) return 1;
    if (stay) QThread::sleep(60);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    g_self = argv[0];
    // PrivateBus re-runs this binary as the guard (and the rows re-run it as the
    // children): dispatch them before anything else, never falling through to the suite.
    if (argc == 3 && std::string(argv[1]) == "--bus-guard") return private_bus::bus_guard(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "--bus-child") return bus_child(argv[2], true);
    if (argc == 3 && std::string(argv[1]) == "--bus-child-exit") return bus_child(argv[2], false);
    QCoreApplication app(argc, argv);

    std::printf("GH #363 - private bus cleanup (Linux)\n");
    std::printf("=====================================\n\n");
    test_bus_cleanup();
    test_bus_normal_exit();
    std::printf("  Group: CS bus cleanup - done\n");
    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
