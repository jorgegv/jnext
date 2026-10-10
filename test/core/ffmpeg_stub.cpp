// Stand-in `ffmpeg` for video_recorder_cmd_test's stop-failure rows (GH #86).
//
// A real program rather than a shell script (GH #214): the Windows recorder
// starts ffmpeg with CreateProcess (src/core/win_process.h), which runs an .exe
// and nothing else, so the same stub has to exist on every OS. Behaviour is the
// one the POSIX shell stub had, selected by JNEXT_TEST_FFMPEG_MODE:
//
//   `-version` as the first argument         -> exit 0 (the availability probe)
//   encode-fail    0-byte output, exit 42
//   partial-fail   non-empty output, exit 42
//   empty-success  0-byte output, exit 0
//   success        non-empty output, exit 0
//   anything else  exit 99
//
// The output file is the LAST argument, as the recorder builds the command.

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int write_out(const char* path, const char* data) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return 1;
    std::fputs(data, f);
    std::fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "-version") == 0) return 0;
    const char* out = argc > 1 ? argv[argc - 1] : "";
    const char* mode = std::getenv("JNEXT_TEST_FFMPEG_MODE");
    if (!mode) mode = "";
    if (std::strcmp(mode, "encode-fail") == 0)   { write_out(out, "");            return 42; }
    if (std::strcmp(mode, "partial-fail") == 0)  { write_out(out, "partialdata"); return 42; }
    if (std::strcmp(mode, "empty-success") == 0) { write_out(out, "");            return 0; }
    if (std::strcmp(mode, "success") == 0)       { write_out(out, "x");           return 0; }
    return 99;
}
