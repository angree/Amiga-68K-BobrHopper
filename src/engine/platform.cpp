#include "platform.h"

#include "gl_api.h"
#include "log.h"

namespace cr {

// O19: ask for 24 bits of depth before settling for 16. The shadows are flattened geometry laid just above the
// floor, so they live or die by depth precision: over the camera's 60-unit range a 16-bit buffer resolves about
// 0.0009 of a unit, and on the R36S (Mali-G31) whole triangles of a shadow dropped out as the camera moved,
// while a still camera looked fine. The renderer also applies a polygon offset now, but the deeper buffer is
// what removes the cause rather than papering over it.
static SDL_GLContext createContext(SDL_Window *win, bool es, int forceDepth)
{
    const int wanted[2] = {forceDepth ? forceDepth : 24, forceDepth ? forceDepth : 16};
    for (int attempt = 0; attempt < 2; attempt++) {
        const int depth = wanted[attempt];
        SDL_GL_ResetAttributes();
        if (es) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        } else {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        }
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depth);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
        SDL_GLContext c = SDL_GL_CreateContext(win);
        if (!c) continue;
        // A driver may hand back the nearest config rather than refusing, and "24 bits of depth, no stencil" is a
        // real answer. The shadow pass needs the stencil to stop overlapping shadows darkening a floor twice, so
        // a config without one is worse than a shallower depth buffer: drop it and try the next depth.
        int gotStencil = 0;
        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &gotStencil);
        if (gotStencil >= 8 || attempt == 1) return c; // the second attempt is the last: take what there is
        SDL_GL_DeleteContext(c);
    }
    return nullptr;
}

bool Platform::init(const PlatformConfig &cfg)
{
    headless_ = cfg.headless;
    hidden_ = cfg.hidden;
    t0_ = SDL_GetPerformanceCounter();
    if (headless_) {
        if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
            logf("FATAL SDL_Init(headless): %s", SDL_GetError());
            return false;
        }
        w_ = cfg.width;
        h_ = cfg.height;
        logf("platform: headless %dx%d", w_, h_);
        return true;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        logf("FATAL SDL_Init: %s", SDL_GetError());
        return false;
    }
    logf("platform: video driver %s", SDL_GetCurrentVideoDriver());
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0) logf("platform: display %dx%d @%dHz", mode.w, mode.h, mode.refresh_rate);

    Uint32 flags = SDL_WINDOW_OPENGL;
    if (cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (cfg.hidden) flags |= SDL_WINDOW_HIDDEN;
    int ww = cfg.hidden ? 64 : cfg.width, wh = cfg.hidden ? 64 : cfg.height;

    for (int attempt = 0; attempt < 2 && !ctx_; attempt++) {
        bool es = attempt == 0;
        win_ = SDL_CreateWindow(cfg.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
        if (!win_) {
            logf("platform: SDL_CreateWindow failed: %s", SDL_GetError());
            continue;
        }
        ctx_ = createContext(win_, es, cfg.depthBits);
        if (!ctx_) {
            logf("platform: %s context failed: %s", es ? "GLES2" : "GL2.1", SDL_GetError());
            SDL_DestroyWindow(win_);
            win_ = nullptr;
        } else {
            gl::setDesktop(!es);
        }
    }
    if (!ctx_) {
        logf("FATAL no GL context");
        return false;
    }
    const char *missing = nullptr;
    if (!gl::load(&missing)) {
        logf("FATAL GL function missing: %s", missing);
        return false;
    }
    SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0);
    SDL_GL_GetDrawableSize(win_, &w_, &h_);
    if (cfg.hidden) {
        w_ = cfg.width;
        h_ = cfg.height;
    }
    // what the driver actually handed over, not what was asked for: the shadows depend on it, and without this
    // line a report of flickering shadows leaves nothing to reason from (O19)
    int gotDepth = 0, gotStencil = 0;
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &gotDepth);
    SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &gotStencil);
    depthBits_ = gotDepth;
    stencilBits_ = gotStencil;
    logf("platform: GL %s | %s | %s | desktop=%d size=%dx%d depth=%d stencil=%d", glGetString(GL_VENDOR),
         glGetString(GL_RENDERER), glGetString(GL_VERSION), gl::isDesktop() ? 1 : 0, w_, h_, gotDepth, gotStencil);
    return true;
}

void Platform::shutdown()
{
    if (ctx_) SDL_GL_DeleteContext(ctx_);
    if (win_) SDL_DestroyWindow(win_);
    ctx_ = nullptr;
    win_ = nullptr;
    SDL_Quit();
}

bool Platform::pump()
{
    events_.clear();
    bool keep = true;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) keep = false;
        events_.push_back(ev);
    }
    return keep;
}

void Platform::swap()
{
    if (win_) SDL_GL_SwapWindow(win_);
}

double Platform::now() const
{
    return double(SDL_GetPerformanceCounter() - t0_) / double(SDL_GetPerformanceFrequency());
}

} // namespace cr
