#include "platform/sdl_app.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>

static void crash_handler(int sig) {
    fprintf(stderr, "\n[CRASH] signal %d received\n", sig);
    fflush(stderr);
    _Exit(1);
}

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);
    SdlApp app;
    if (!app.init(argc, argv)) return 1;
    app.run();
    app.shutdown();
    return 0;
}
