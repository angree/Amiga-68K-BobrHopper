#include "ui/controls.h"

#include "game/game.h"
#include "ui/screens.h"

namespace cr {

int playerDevice(const UserSettings &settings, int player)
{
    if (settings.players < 2) return 0; // every device at once
    const int p = player == 1 ? 1 : 0;
    const int d = settings.control[p] + 1; // the settings list is 0-based, Input's devices start at 1
    return d > 0 && d < kInputDevices ? d : 0;
}

void applyPlayerInput(const Input &input, const UserSettings &settings, Game &game)
{
    // the original's GestureView: a key going down starts the squat, letting it go performs the hop. A is a hop
    // forward as well as the confirm button, which is why it appears twice here.
    static const struct {
        Action act;
        Swipe dir;
    } dirs[] = {{ActUp, Swipe::Up}, {ActDown, Swipe::Down}, {ActLeft, Swipe::Left}, {ActRight, Swipe::Right},
                {ActA, Swipe::Up}};
    for (int p = 0; p < game.playerCount(); p++) {
        const int device = playerDevice(settings, p);
        for (int i = 0; i < 5; i++) {
            if (input.devicePressed(device, dirs[i].act)) game.beginMoveWithDirection(p);
            if (input.deviceReleased(device, dirs[i].act)) game.moveWithDirection(dirs[i].dir, p);
        }
    }
}

} // namespace cr
