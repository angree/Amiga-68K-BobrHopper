// SDL window + GL context. On PC automated runs use a hidden window and render into an FBO,
// so tests never pop up a window or take keyboard focus from the user's desktop.
#pragma once

#include <SDL.h>

#include <vector>

namespace cr {

struct PlatformConfig {
    // O19 (diagnostics): force the depth buffer the context asks for, so the device's 16-bit buffer - where the
    // shadows flickered - can be reproduced on a PC that would otherwise hand out 24 bits. 0 = ask for the best.
    int depthBits = 0;
    int width = 640;
    int height = 480;
    bool fullscreen = false; // device: fullscreen desktop mode at native resolution
    bool hidden = false;     // PC automation: invisible window, draw into a RenderTarget
    bool headless = false;   // no video at all (logic tests, qemu)
    bool vsync = true;
    const char *title = "BobrHopper";
};

class Platform {
public:
    bool init(const PlatformConfig &cfg);
    void shutdown();

    // Moves pending SDL events into events(); returns false when the app should quit.
    bool pump();
    const std::vector<SDL_Event> &events() const { return events_; }

    void swap();
    double now() const;

    int width() const { return w_; }
    int height() const { return h_; }
    // O19: what the driver actually granted. The shadow pass masks itself with the stencil, and a context without
    // one throws the shadows away instead of merely double-darkening them, so the renderer has to know.
    int stencilBits() const { return stencilBits_; }
    int depthBits() const { return depthBits_; }
    bool headless() const { return headless_; }
    bool hidden() const { return hidden_; }
    SDL_Window *window() const { return win_; }

private:
    SDL_Window *win_ = nullptr;
    SDL_GLContext ctx_ = nullptr;
    int w_ = 0, h_ = 0;
    int depthBits_ = 0, stencilBits_ = 0; // what the driver granted, not what was asked for (O19)
    bool headless_ = false, hidden_ = false;
    std::vector<SDL_Event> events_;
    Uint64 t0_ = 0;
};

} // namespace cr
