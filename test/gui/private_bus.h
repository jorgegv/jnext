// ===========================================================================
// A private session bus for tests (GH #363, #414): a dbus-daemon under a
// guard process, shared by color_scheme_test (CS-18..22, CS-30) and
// color_scheme_linux_test (CS-28/29). Header-only; the includer defines
// nothing else. g_self must be set to argv[0] in main(): PrivateBus re-runs
// that binary as --bus-guard, so every includer's main() must dispatch it
// (bus_guard) before doing anything else.
// ===========================================================================
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cstdio>
#include <string>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

namespace private_bus {

// The private bus main() starts; empty when dbus-daemon could not be started.
inline QByteArray g_bus_address;
inline std::string g_bus_error;

// A private session bus for CS-18..22: a dbus-daemon with a minimal config
// (no service directories, so nothing is ever activated), its socket in a
// temporary directory, its environment explicit (LANG=C, CLAUDE.md).
//
// The daemon runs --nofork under a GUARD: this binary re-run as --bus-guard,
// which starts the daemon and SIGKILLs it as soon as its stdin reaches EOF.
// Its stdin is a pipe whose only writer is this suite, so the daemon goes
// with the suite however the suite ends — including a SIGKILL from the
// harness on a timeout, when no destructor runs. (PR_SET_PDEATHSIG on the
// daemon does not survive: measured on dbus-daemon 1.16, a --nofork daemon
// with PDEATHSIG=SIGKILL outlives its SIGKILLed parent.) CS-28 pins it.
inline const char* g_self = nullptr;

struct PrivateBus {
    QTemporaryDir dir;
    QProcess guard;
    qint64 daemon_pid = 0;
    PrivateBus() {
        if (!dir.isValid()) { g_bus_error = "no temporary directory"; return; }
        const QString conf = dir.filePath("bus.conf");
        QFile f(conf);
        if (!f.open(QIODevice::WriteOnly)) { g_bus_error = "cannot write bus.conf"; return; }
        f.write(QStringLiteral(
                    "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" "
                    "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                    "<busconfig><type>session</type><listen>unix:dir=%1</listen>"
                    "<auth>EXTERNAL</auth><policy context=\"default\"><allow send_destination=\"*\"/>"
                    "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n")
                    .arg(dir.path()).toUtf8());
        f.close();
        QProcessEnvironment env;
        env.insert("LANG", "C");
        env.insert("PATH", qEnvironmentVariable("PATH"));
        guard.setProcessEnvironment(env);
        guard.start(QString::fromLocal8Bit(g_self), {"--bus-guard", conf});
        if (!guard.waitForStarted(5000)) { g_bus_error = "the bus guard did not start"; return; }
        QElapsedTimer t;
        t.start();
        do {   // at least one poll, however late this thread is first scheduled (GH #399)
            guard.waitForReadyRead(100);
            const QByteArray err = guard.readAllStandardError();
            if (const int i = err.indexOf("pid="); i >= 0) daemon_pid = err.mid(i + 4).trimmed().toLongLong();
        } while (!(guard.canReadLine() && daemon_pid > 0) && t.elapsed() < 5000);
        g_bus_address = guard.readLine().trimmed();
        if (g_bus_address.isEmpty()) g_bus_error = "dbus-daemon did not start (is it installed?)";
    }
    ~PrivateBus() {
        guard.closeWriteChannel();   // EOF: the guard kills the daemon and exits
        if (!guard.waitForFinished(5000)) { guard.kill(); guard.waitForFinished(3000); }
    }
};

#ifndef _WIN32
// --bus-guard CONF: run dbus-daemon on CONF (its address on our stdout), print
// "pid=N" on stderr, and SIGKILL it when stdin reaches EOF. Plain POSIX: it
// must not depend on anything a dying parent could leave half-done.
inline int bus_guard(const char* conf) {
    const pid_t d = fork();
    if (d < 0) return 1;
    if (d == 0) {
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);   // the daemon must not hold the guard's stdin
        const std::string cfg = std::string("--config-file=") + conf;
        execlp("dbus-daemon", "dbus-daemon", cfg.c_str(), "--nofork", "--print-address=1",
               static_cast<char*>(nullptr));
        _exit(127);
    }
    std::fprintf(stderr, "pid=%d\n", static_cast<int>(d));
    std::fflush(stderr);
    char buf[64];
    while (read(0, buf, sizeof buf) > 0) {}
    kill(d, SIGKILL);
    waitpid(d, nullptr, 0);
    return 0;
}
#endif

} // namespace private_bus
