#include "ui/hud.h"

#include <algorithm>
#include <string>

#include "engine/strings.h"
#include "game/game.h"
#include "ui/lang.h"
#include "ui/ranks.h"

namespace cr {

void drawHud(Renderer &renderer, TextRenderer &text, const Game &game, int screenW, int screenH)
{
    const int left = 8, top = 16, gap = 12;
    const Rgba white{1, 1, 1, 1}, black{0, 0, 0, 1}, yellow{1, 1, 0, 1};
    renderer.beginOverlay(screenW, screenH);
    if (game.state() == GameState::None) {
        // O11.9: on the menu only the Classic best stays - the score and the level counters belong to a game (the
        // rank of the career is drawn by the menu itself)
        if (game.highscore() > 0)
            text.drawOutlined(renderer, std::string(lang::t(lang::Top)) + " " + toString(game.highscore()), left, top,
                              14, yellow, 2, black);
    } else if (game.level() > 0) {
        // O11.3 Progression: the rows crossed of the level on the left, the way through it in percent on the right
        const int total = game.levelRows();
        const int passed = std::min(game.score(), total);
        const int size = 32;
        text.drawOutlined(renderer, toString(passed) + "/" + toString(total), left, top, size, white, 3, black);
        const std::string percent = toString(total > 0 ? passed * 100 / total : 0) + "%";
        text.drawOutlined(renderer, percent, screenW - 8 - text.width(percent, size), top, size, white, 3, black);
        text.drawOutlined(renderer, std::string(lang::t(lang::Level)) + " " + levelLabel(game.level()), left,
                          top + text.lineHeight(size) + gap, 14, yellow, 2, black);
    } else if (game.playerCount() > 1) {
        // O23 Classic with two players is a duel, so each of them has a score of its own: the first on the left, the
        // second on the right. A player who is out is greyed, which is how you see who is still in it.
        // "PLAYER 1 - 9" on one line, as the user asked for: the name, a dash, the score. A player who is out of
        // the game is greyed, which is how you see at a glance who is still in it.
        const int size = 18; // only the baked sizes exist (data/fonts/retro_*.fnt): 12, 14, 16, 18, 32, 48
        const Rgba grey{0.6f, 0.6f, 0.6f, 1};
        const std::string one = std::string(lang::t(lang::PlayerOne)) + " - " + toString(game.score(0));
        const std::string two = std::string(lang::t(lang::PlayerTwo)) + " - " + toString(game.score(1));
        text.drawOutlined(renderer, one, left, top, size, game.hero(0).isAlive ? white : grey, 2, black);
        text.drawOutlined(renderer, two, screenW - 8 - text.width(two, size), top, size,
                          game.hero(1).isAlive ? white : grey, 2, black);
    } else {
        text.drawOutlined(renderer, toString(game.score()), left, top, 48, white, 4, black);
        if (game.highscore() > 0)
            text.drawOutlined(renderer, std::string(lang::t(lang::Top)) + " " + toString(game.highscore()), left,
                              top + text.lineHeight(48) + gap, 14, yellow, 2, black);
    }
    renderer.endOverlay();
}

} // namespace cr
