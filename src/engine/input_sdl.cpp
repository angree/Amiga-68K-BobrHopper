// SDL devices for Input (PC keyboard, R36S game controller / raw joystick).
#include "input.h"

#include <SDL.h>

#include "log.h"

namespace cr {

static const int kStickDeadzone = int(0.4 * 32767); // same as OpenSWOS on the R36S

static uint16_t keyAction(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: case SDLK_w: return ActUp;
    case SDLK_DOWN: case SDLK_s: return ActDown;
    case SDLK_LEFT: case SDLK_a: return ActLeft;
    case SDLK_RIGHT: case SDLK_d: return ActRight;
    case SDLK_SPACE: case SDLK_RETURN: case SDLK_z: return ActA;
    case SDLK_x: case SDLK_BACKSPACE: return ActB;
    case SDLK_ESCAPE: case SDLK_p: return ActStart;
    case SDLK_TAB: return ActSelect;
    case SDLK_q: return ActL;
    case SDLK_e: return ActR;
    default: return 0;
    }
}

// O23: the keyboard as TWO devices, so two players can share it - arrows with space and enter for one, WSAD with
// the left shift for the other. keys_ is left exactly as it was, so nothing changes for a single player; these only
// fill the per-device masks the settings screen hands out.
static uint16_t arrowsAction(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return ActUp;
    case SDLK_DOWN: return ActDown;
    case SDLK_LEFT: return ActLeft;
    case SDLK_RIGHT: return ActRight;
    case SDLK_SPACE: case SDLK_RETURN: return ActA;
    case SDLK_BACKSPACE: return ActB;
    default: return 0;
    }
}

static uint16_t wasdAction(SDL_Keycode k)
{
    switch (k) {
    case SDLK_w: return ActUp;
    case SDLK_s: return ActDown;
    case SDLK_a: return ActLeft;
    case SDLK_d: return ActRight;
    case SDLK_LSHIFT: case SDLK_LCTRL: return ActA;
    default: return 0;
    }
}

static uint16_t buttonAction(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return ActUp;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ActDown;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ActLeft;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ActRight;
    case SDL_CONTROLLER_BUTTON_A: return ActA;
    case SDL_CONTROLLER_BUTTON_B: return ActB;
    case SDL_CONTROLLER_BUTTON_START: return ActStart;
    case SDL_CONTROLLER_BUTTON_BACK: return ActSelect;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return ActL;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ActR;
    default: return 0;
    }
}

void Input::init()
{
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    // PortMaster's get_controls on ArkOS provides the pad mappings as a file (SDL_GAMECONTROLLERCONFIG_FILE, e.g.
    // /tmp/gamecontrollerdb.txt, seen in the OpenSWOS log on the R36S); older system SDL2 does not read that
    // variable itself, so load it here
    if (const char *file = SDL_getenv("SDL_GAMECONTROLLERCONFIG_FILE")) {
        if (*file) {
            int added = SDL_GameControllerAddMappingsFromFile(file);
            logf("input: %d controller mappings from %s", added, file);
        }
    }
    int n = SDL_NumJoysticks();
    logf("input: %d joystick(s)", n);
    for (int i = 0; i < n; i++) openController(i);
}

// O23: the slot a pad event belongs to, by SDL's instance id. -1 for a third pad, which nothing reads.
int Input::padSlot(int32_t which) const
{
    for (size_t i = 0; i < padIds_.size() && i < 2; i++)
        if (padIds_[i] == which) return int(i);
    return -1;
}

int Input::padCount() const { return int(padIds_.size() > 2 ? 2 : padIds_.size()); }

void Input::openController(int index)
{
    if (SDL_IsGameController(index)) {
        SDL_GameController *gc = SDL_GameControllerOpen(index);
        if (gc) {
            controllers_.push_back(gc);
            padIds_.push_back(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc)));
            char *mapping = SDL_GameControllerMapping(gc);
            logf("input: controller %d '%s' mapping: %s", index, SDL_GameControllerName(gc), mapping ? mapping : "-");
            SDL_free(mapping);
            return;
        }
    }
    SDL_Joystick *js = SDL_JoystickOpen(index);
    if (js) {
        joysticks_.push_back(js);
        padIds_.push_back(SDL_JoystickInstanceID(js));
        logf("input: raw joystick %d '%s' buttons=%d axes=%d hats=%d (no controller mapping)", index,
             SDL_JoystickName(js), SDL_JoystickNumButtons(js), SDL_JoystickNumAxes(js), SDL_JoystickNumHats(js));
    }
}

void Input::shutdown()
{
    flushRecording();
    for (void *gc : controllers_) SDL_GameControllerClose(static_cast<SDL_GameController *>(gc));
    for (void *js : joysticks_) SDL_JoystickClose(static_cast<SDL_Joystick *>(js));
    controllers_.clear();
    joysticks_.clear();
    padIds_.clear();
}

void Input::handleEvents(const std::vector<SDL_Event> &events)
{
    for (const SDL_Event &ev : events) {
        switch (ev.type) {
        case SDL_KEYDOWN:
            if (!ev.key.repeat) {
                keys_ |= keyAction(ev.key.keysym.sym);
                keysArrows_ |= arrowsAction(ev.key.keysym.sym);
                keysWasd_ |= wasdAction(ev.key.keysym.sym);
            }
            break;
        case SDL_KEYUP:
            keys_ &= uint16_t(~keyAction(ev.key.keysym.sym));
            keysArrows_ &= uint16_t(~arrowsAction(ev.key.keysym.sym));
            keysWasd_ &= uint16_t(~wasdAction(ev.key.keysym.sym));
            break;
        case SDL_CONTROLLERBUTTONDOWN: {
            pad_ |= buttonAction(ev.cbutton.button);
            const int slot = padSlot(ev.cbutton.which);
            if (slot >= 0) padBtn_[slot] |= buttonAction(ev.cbutton.button);
            if (logRawButtons)
                logf("input: pad %d button %s down", slot, SDL_GameControllerGetStringForButton(SDL_GameControllerButton(ev.cbutton.button)));
            break;
        }
        case SDL_CONTROLLERBUTTONUP: {
            pad_ &= uint16_t(~buttonAction(ev.cbutton.button));
            const int slot = padSlot(ev.cbutton.which);
            if (slot >= 0) padBtn_[slot] &= uint16_t(~buttonAction(ev.cbutton.button));
            break;
        }
        case SDL_CONTROLLERAXISMOTION: {
            int v = ev.caxis.value;
            const int slot = padSlot(ev.caxis.which);
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                stick_ &= uint16_t(~(ActLeft | ActRight));
                if (slot >= 0) padStick_[slot] &= uint16_t(~(ActLeft | ActRight));
                if (v < -kStickDeadzone) {
                    stick_ |= ActLeft;
                    if (slot >= 0) padStick_[slot] |= ActLeft;
                }
                if (v > kStickDeadzone) {
                    stick_ |= ActRight;
                    if (slot >= 0) padStick_[slot] |= ActRight;
                }
            } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                stick_ &= uint16_t(~(ActUp | ActDown));
                if (slot >= 0) padStick_[slot] &= uint16_t(~(ActUp | ActDown));
                if (v < -kStickDeadzone) {
                    stick_ |= ActUp;
                    if (slot >= 0) padStick_[slot] |= ActUp;
                }
                if (v > kStickDeadzone) {
                    stick_ |= ActDown;
                    if (slot >= 0) padStick_[slot] |= ActDown;
                }
            }
            break;
        }
        case SDL_JOYHATMOTION: {
            if (!controllers_.empty()) break;
            hat_ = 0;
            if (ev.jhat.value & SDL_HAT_UP) hat_ |= ActUp;
            if (ev.jhat.value & SDL_HAT_DOWN) hat_ |= ActDown;
            if (ev.jhat.value & SDL_HAT_LEFT) hat_ |= ActLeft;
            if (ev.jhat.value & SDL_HAT_RIGHT) hat_ |= ActRight;
            const int slot = padSlot(ev.jhat.which);
            if (slot >= 0) padHat_[slot] = hat_;
            break;
        }
        case SDL_JOYBUTTONDOWN: {
            if (logRawButtons && controllers_.empty()) logf("input: raw joystick button %d down", ev.jbutton.button);
            // without a mapping: treat the first buttons like A, B, ... so the game is still usable
            if (controllers_.empty() && ev.jbutton.button < 8) {
                static const uint16_t fallback[8] = {ActA, ActB, ActA, ActB, ActL, ActR, ActSelect, ActStart};
                rawButtons_ |= fallback[ev.jbutton.button];
                const int slot = padSlot(ev.jbutton.which);
                if (slot >= 0) padRaw_[slot] |= fallback[ev.jbutton.button];
            }
            break;
        }
        case SDL_JOYBUTTONUP: {
            if (controllers_.empty() && ev.jbutton.button < 8) {
                static const uint16_t fallback[8] = {ActA, ActB, ActA, ActB, ActL, ActR, ActSelect, ActStart};
                rawButtons_ &= uint16_t(~fallback[ev.jbutton.button]);
                const int slot = padSlot(ev.jbutton.which);
                if (slot >= 0) padRaw_[slot] &= uint16_t(~fallback[ev.jbutton.button]);
            }
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            if (!SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(ev.cdevice.which)))
                openController(ev.cdevice.which);
            break;
        default:
            break;
        }
    }
    // O23: the devices the settings screen offers here, in its own order: 1 arrows, 2 WSAD, 3 and 4 the pads.
    // A single player reads device 0, which is all of them together, so this changes nothing for one player.
    setDevice(1, keysArrows_);
    setDevice(2, keysWasd_);
    for (int slot = 0; slot < 2; slot++)
        setDevice(3 + slot, uint16_t(padBtn_[slot] | padStick_[slot] | padHat_[slot] | padRaw_[slot]));
}

} // namespace cr
