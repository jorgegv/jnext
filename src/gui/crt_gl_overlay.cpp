#ifdef JNEXT_HAVE_CRT_GL

#include "gui/crt_gl_overlay.h"
#include "gui/crt_gl_loader.h"
#include "core/log.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <chrono>
#include <cstring>

#include "crtemu/crtemu.h"

namespace {

// crtemu is a C library with no user-data pointer on its GL hooks, so the
// overlay that is currently inside initializeGL()/paintGL() publishes its FBO
// here. Both run on the GUI thread, one overlay at a time.
unsigned g_screen_fbo = 0;

int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void* crt_gl_get_proc(const char* name) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void*>(ctx->getProcAddress(name)) : nullptr;
}

unsigned crt_gl_screen_fbo() { return g_screen_fbo; }

void crt_gl_report(const char* text) {
    Log::platform()->error("crtemu: {}", text);
}

CrtGlOverlay::CrtGlOverlay(QWidget* parent, crt_shader::Style style)
    : QOpenGLWidget(parent), style_(style), t0_us_(now_us())
{
    // crtemu's shaders are GLSL 1.20, which only a 2.1 / compatibility context
    // accepts: ask for exactly that (on macOS this selects the legacy profile —
    // a 3.2+ core context would reject #version 120).
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::NoProfile);
    setFormat(fmt);

    // The picture is for looking at. Input belongs to the EmulatorWidget below.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
}

CrtGlOverlay::~CrtGlOverlay() {
    // ~QOpenGLWidget destroys the context (emitting aboutToBeDestroyed) before this
    // object's own connections are auto-removed: cut ours first.
    QObject::disconnect(ctx_conn_);
    release_crtemu();
}

void CrtGlOverlay::release_crtemu() {
    if (!crt_) return;
    // Without a live context its GL objects are already gone: free the struct alone,
    // because calling GL through it would be undefined.
    if (isValid()) {
        makeCurrent();
        g_screen_fbo = defaultFramebufferObject();
        crtemu_destroy(crt_);
        doneCurrent();
    } else {
        crt_gl_free_struct(crt_);
    }
    crt_ = nullptr;
}

void CrtGlOverlay::submit(const uint32_t* argb, int w, int h) {
    if (!argb || w <= 0 || h <= 0) return;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    if (abgr_.size() != n) abgr_.resize(n);
    crt_shader::argb_to_abgr(argb, abgr_.data(), n);
    frame_w_ = w;
    frame_h_ = h;
    update();
}

bool CrtGlOverlay::copy_frame_argb(std::vector<uint32_t>& out) const {
    if (abgr_.empty()) return false;
    out.resize(abgr_.size());
    crt_shader::argb_to_abgr(abgr_.data(), out.data(), abgr_.size());   // the swap is its own inverse
    return true;
}

void CrtGlOverlay::initializeGL() {
    // initializeGL() runs again if Qt recreates the context: drop the old crtemu
    // first, and tear down while the context is still current when it is destroyed.
    release_crtemu();
    QObject::disconnect(ctx_conn_);
    ctx_conn_ = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
                        [this] { release_crtemu(); }, Qt::DirectConnection);
    g_screen_fbo = defaultFramebufferObject();
    crt_ = crtemu_create(static_cast<crtemu_type_t>(style_), nullptr);
    if (crt_) {
        // crtemu's optional bezel texture is bound to a sampler every frame; with no
        // storage the driver logs "unit 2 is unloadable" each time. Give it a 1x1
        // texture, then switch the bezel off again (a null frame clears use_frame).
        const uint32_t clear = 0;
        crtemu_frame(crt_, const_cast<uint32_t*>(&clear), 1, 1);
        crtemu_frame(crt_, nullptr, 1, 1);
    }
    if (!crt_ && !failure_reported_) {
        failure_reported_ = true;
        emit failed(tr("the effect could not be built on this graphics driver"));
    }
}

void CrtGlOverlay::paintGL() {
    if (!crt_) return;

    g_screen_fbo = defaultFramebufferObject();
    const qreal dpr = devicePixelRatioF();
    QOpenGLFunctions* gl = context()->functions();
    if (abgr_.empty()) {
        // No frame yet (shader on before the first one): black, not an undefined FBO.
        gl->glClearColor(0.f, 0.f, 0.f, 1.f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    gl->glViewport(0, 0, qRound(width() * dpr), qRound(height() * dpr));

    crtemu_present(crt_, static_cast<CRTEMU_U64>(now_us() - t0_us_),
                   abgr_.data(), frame_w_, frame_h_,
                   0xffffffffu /* no colour modulation */,
                   0xff000000u /* black outside the curved screen */);
    emit presented();
}

void CrtGlOverlay::paintEvent(QPaintEvent* event) {
    QOpenGLWidget::paintEvent(event);
    // Qt could not create a context (or an FBO) for this widget: initializeGL()
    // never ran and paintGL() never will, so the picture would stay black.
    if (!isValid() && !failure_reported_) {
        failure_reported_ = true;
        emit failed(tr("no usable graphics context is available"));
    }
}

#endif  // JNEXT_HAVE_CRT_GL
