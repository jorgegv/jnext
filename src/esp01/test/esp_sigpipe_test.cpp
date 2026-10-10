// SIGPIPE protection of the ESP-01 socket transport (GH #25, GH #214).
//
// These two rows used to be the SIG block of esp_socket_test. They fork() — the
// failure they guard is the death of the process running the assertions — and
// SIGPIPE does not exist on Windows, so they live in a suite of their own that
// the manifest declares `# os: posix`. Rows, IDs and assertions are unchanged;
// the socket scaffolding is shared through esp_test_net.h.
//
// Run: ./build/test/esp_sigpipe_test

#include "esp01/esp_socket.h"

#include "esp_test_net.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace esp;

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

// Row-ID report for jnext's unit-test harness: test/row_id.h's report_row_id(),
// restated because this self-contained module cannot include jnext's test tree
// (see esp_socket_test.cpp).
static void report_row_id(const char* id) {
    static std::FILE* const out = [] {
        const char* path = std::getenv("JNEXT_TEST_ROW_IDS");
        return (path && *path) ? std::fopen(path, "ab") : nullptr;   // never truncate
    }();
    if (!out) return;
    std::fprintf(out, "%s\n", id);
    std::fflush(out);   // nothing buffered across the fork()
}

static void check(const char* id, const std::string& desc, bool cond) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc.c_str());
    }
}

static void skip(const char* id, const char* why) {
    report_row_id(id);
    ++g_total;
    ++g_skip;
    std::printf("  SKIP %s: %s\n", id, why);
}

int main() {
    net_init();
    std::printf("\n======================================================\n");
    std::printf("ESP-01 socket transport: SIGPIPE protection (GH #25)\n");
    std::printf("======================================================\n\n");

    // ═══ SIG — a dead peer must not take the emulator down with it ═════════
    // esp_socket_posix.cpp sets MSG_NOSIGNAL (Linux) / SO_NOSIGPIPE (BSD)
    // because without one of them a write to a closed peer raises SIGPIPE and
    // the DEFAULT disposition terminates the process. Every other row here
    // sends only while the connection is provably open, or after the transport
    // has already self-transitioned — where send() short-circuits before the
    // OS call and the flag is moot. So none of them can see the protection
    // disappear.
    //
    // The reachable path is a caller that WRITES without reading: send() never
    // detects EOF (only recv() does), so a blind write after an orderly peer
    // close goes out once, draws a RST, and the next one hits EPIPE. Unguarded
    // that is a signal, not an errno.
    //
    // It has to run in a forked child because the failure mode is the death of
    // the process running the assertions.
    {
        const pid_t pid = ::fork();
        if (pid < 0) {
            skip("SIG-01", "fork() unavailable on this host");
            skip("SIG-02", "fork() unavailable on this host");
        } else if (pid == 0) {
            // CHILD. Deliberately no printf: stdout is buffered and inherited,
            // so anything written here would be duplicated by the parent. The
            // exit code carries the verdict, and _exit() skips the shared
            // stdio flush for the same reason.
            Listener l;
            if (!l.start()) ::_exit(5);
            auto t = make_socket_transport(loopback_ok());
            if (!t->begin_connect("127.0.0.1", l.port())) ::_exit(5);
            if (!pump_until(*t, TransportState::Connected)) ::_exit(5);
            const int srv = l.accept_one(1000);
            if (srv < 0) ::_exit(5);
            net_close(srv);  // orderly close by the peer — and we never recv()

            const std::uint8_t buf[4] = {'A', 'T', '\r', '\n'};
            for (int i = 0; i < 200 && t->state() == TransportState::Connected; ++i) {
                t->send(buf, sizeof(buf));  // blind write into a dead peer
                sleep_ms(2);
            }
            if (t->state() != TransportState::Failed) ::_exit(3);
            if (t->last_error().empty()) ::_exit(4);
            ::_exit(0);
        } else {
            int status = 0;
            ::waitpid(pid, &status, 0);
            const bool setup_failed = WIFEXITED(status) && WEXITSTATUS(status) == 5;
            if (setup_failed) {
                skip("SIG-01", "the child could not set up a loopback connection");
                skip("SIG-02", "the child could not set up a loopback connection");
            } else {
                check("SIG-01",
                      "a blind send to a closed peer does not signal-kill the process",
                      !WIFSIGNALED(status));
                check("SIG-02",
                      "...and surfaces as Failed with an error string instead",
                      WIFEXITED(status) && WEXITSTATUS(status) == 0);
            }
        }
    }


    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
