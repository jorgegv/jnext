#include "platform/sdl_app.h"

int main(int argc, char* argv[]) {
    SdlApp app;
    if (!app.init(argc, argv)) return 1;
    app.run();
    app.shutdown();
    return 0;
}
