// crtshot - one picture per CRT effect (off, Scanlines, TV, PC monitor) of the
// SAME emulator frame.
// ===========================================================================
//
// WHY A TOOL OF ITS OWN. jnext's --delayed-screenshot writes the emulator's own
// framebuffer, before any display filter (doc/design/CRT-GL-FILTER.md: the effect
// is display only), and tools/docshot runs on the offscreen Qt platform, which has
// no GPU. A picture of the CRT effect needs a real window and a real OpenGL
// context, so this tool runs on the native platform.
//
// WHAT IT DOES. Boots a machine from the SD image (optionally loading a program),
// runs it for N frames, keeps THAT frame, then draws it through an EmulatorWidget
// once per effect and saves what the widget shows. Every mode therefore shows the
// identical frame, taken at the identical moment. The TV and PC looks carry a
// little animated noise and flicker, so those two differ slightly from run to run.
//
// The SD image is copied first (jnext opens an image read-write and NextZXOS
// writes to it), so the master is never touched.
//
// Run: ./build/crtshot --prefix boot
//      ./build/crtshot --prefix game --frames 900 --load /path/to/Program.nex

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/sdcard_provisioner.h"
#include "gui/emulator_widget.h"
#include "platform/emulator_boot.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr const char* kRtc = "2026-07-10T08:55:00";   // as docshot: the same clock every run

void settle(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

struct Mode {
    const char* name;
    bool scanlines;
    bool shader;
    crt_shader::Style style;
};

void usage() {
    std::printf(
        "crtshot - one screenshot per CRT effect, of the same emulator frame\n"
        "\n"
        "Usage: crtshot [--out DIR] [--prefix NAME] [--frames N] [--scale N]\n"
        "               [--load FILE] [--sdcard IMAGE]\n"
        "\n"
        "  --out DIR       where to write the PNGs (default src/doc/user-guide/img)\n"
        "  --prefix NAME   files are NAME-off.png, NAME-scanlines.png, NAME-tv.jpg,\n"
        "                  NAME-pc.jpg (default crt). The two animated looks are JPEG:\n"
        "                  their noise makes a PNG about 2 MB\n"
        "  --frames N      frames to run before capturing (default 600: the NextZXOS\n"
        "                  welcome screen)\n"
        "  --scale N       window scale, 2-4 (default 2)\n"
        "  --load FILE     a .nex/.tap/.tzx/... to load at start-up\n"
        "  --sdcard IMAGE  NextZXOS SD image; defaults to $JNEXT_TEST_SD_IMAGE, then\n"
        "                  the location jnext itself falls back to\n"
        "\n"
        "Needs a display; TV and PC also need OpenGL and are skipped with a message\n"
        "if the context cannot be created. Scanlines is the software overlay.\n");
}

} // namespace

int main(int argc, char** argv)
{
    QString out_dir = QStringLiteral("src/doc/user-guide/img");
    QString prefix  = QStringLiteral("crt");
    QString sd, load;
    int frames = 600, scale = 2;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--out" && more)    out_dir = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--prefix" && more) prefix  = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--frames" && more) frames  = std::atoi(argv[++i]);
        else if (a == "--scale" && more)  scale   = std::atoi(argv[++i]);
        else if (a == "--load" && more)   load    = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--sdcard" && more) sd      = QString::fromLocal8Bit(argv[++i]);
        else { usage(); return 2; }
    }
    if (frames < 1 || scale < 2 || scale > 4) { usage(); return 2; }
    if (!EmulatorWidget::crt_shader_built_in()) {
        std::fprintf(stderr, "crtshot: this build has no CRT shader (-DENABLE_CRT_GL=OFF)\n");
        return 1;
    }

    if (sd.isEmpty())
        if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) sd = QString::fromLocal8Bit(env);
    if (sd.isEmpty()) sd = QString::fromStdString(sdcard::default_sdcard_image_path());
    if (!QFileInfo::exists(sd)) {
        std::fprintf(stderr, "crtshot: no NextZXOS SD image at %s\n"
                             "  provision one with ./build/jnext --headless --sdcard-download-confirm\n",
                     qPrintable(sd));
        return 1;
    }
    if (!load.isEmpty() && !QFileInfo::exists(load)) {
        std::fprintf(stderr, "crtshot: no such file: %s\n", qPrintable(load));
        return 1;
    }

    // Work on a copy, and keep jnext's own config out of the picture.
    // Under ~/.jnext/runs, not the system temp dir (RAM-backed on some hosts, and the
    // image is 1 GB), and cloned with cp --reflink=auto like `make docs-screenshots`.
    const QString runs = QDir::homePath() + QStringLiteral("/.jnext/runs");
    QDir().mkpath(runs);
    QTemporaryDir scratch(runs + QStringLiteral("/crtshot-XXXXXX"));
    if (!scratch.isValid()) { std::fprintf(stderr, "crtshot: no temporary directory\n"); return 1; }
    const QString sd_copy = scratch.path() + QStringLiteral("/sd.img");
    std::printf("crtshot: copying the SD image...\n");
    // --reflink is GNU cp only: where cp refuses it (BSD/macOS) or is missing, copy plainly.
    if (QProcess::execute(QStringLiteral("cp"), {QStringLiteral("--reflink=auto"), sd, sd_copy}) != 0 &&
        !QFile::copy(sd, sd_copy)) {
        std::fprintf(stderr, "crtshot: could not copy %s\n", qPrintable(sd));
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", scratch.path().toUtf8());

    QApplication app(argc, argv);   // the native platform: this needs a GPU

    Emulator emu;
    EmulatorConfig cfg;
    cfg.type         = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image = sd_copy.toStdString();
    cfg.load_file    = load.toStdString();
    cfg.rtc_fixed    = parse_rtc_datetime(kRtc, cfg.rtc_fixed_tm);
    if (!emu.init(cfg)) { std::fprintf(stderr, "crtshot: emulator init failed\n"); return 1; }
    std::printf("crtshot: running %d frames%s\n", frames, load.isEmpty() ? "" : " with the program loaded");
    // The program is loaded the way the command line does it: once the machine is
    // up (a tape format that keys through BASIC waits a little first).
    const int load_at = load.isEmpty() ? -1 : emulator_load_delay_frames(load.toStdString());
    for (int i = 0; i < frames; ++i) {
        if (i == load_at && !emulator_apply_load_keeping(emu, load.toStdString(), false, nullptr)) {
            std::fprintf(stderr, "crtshot: could not load %s\n", qPrintable(load));
            return 1;
        }
        emu.run_frame();
    }

    const int fw = emu.get_framebuffer_width(), fh = emu.get_framebuffer_height();
    const std::vector<uint32_t> frame(emu.get_framebuffer(), emu.get_framebuffer() + static_cast<size_t>(fw) * fh);

    QDir().mkpath(out_dir);
    const Mode modes[] = {
        {"off",       false, false, crt_shader::Style::Tv},
        {"scanlines", true,  false, crt_shader::Style::Tv},
        {"tv",        false, true,  crt_shader::Style::Tv},
        {"pc",        false, true,  crt_shader::Style::Pc},
    };
    int failed = 0;
    for (const Mode& m : modes) {
        EmulatorWidget w;
        w.set_scale(scale);
        w.set_crt_shader_style(m.style);
        w.set_crt_filter(m.scanlines);
        w.set_crt_shader(m.shader);
        w.show();
        // The first paint creates the GL context; give it frames to draw.
        for (int k = 0; k < 6; ++k) {
            w.update_frame(frame.data(), fw, fh);
            settle(120);
        }
        const bool gpu = w.crt_shader_active();
        if (m.shader && !gpu) {
            std::fprintf(stderr, "crtshot: %s: no OpenGL here, would show the software display - skipped\n", m.name);
            ++failed;
            continue;
        }
        const QString path = QStringLiteral("%1/%2-%3.%4").arg(out_dir, prefix, QString::fromLatin1(m.name),
                                                                m.shader ? QStringLiteral("jpg") : QStringLiteral("png"));
        const QImage img = w.grab().toImage();
        if (img.isNull() || !img.save(path, nullptr, m.shader ? 92 : -1)) { std::fprintf(stderr, "crtshot: could not write %s\n", qPrintable(path)); ++failed; continue; }
        std::printf("  %s (%dx%d)\n", qPrintable(path), img.width(), img.height());
    }
    return failed ? 1 : 0;
}
