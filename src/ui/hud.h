// HUD from src/components/ScoreText.tsx: the score (retro 48, white, 4 px black text-shadow) and, once there is
// a best score, "TOP n" (retro 14, yellow, 2 px) under it; container at left 8 / top 16, 12 px gap.
#pragma once

#include "engine/renderer.h"
#include "engine/text.h"

namespace cr {

class Game;

void drawHud(Renderer &renderer, TextRenderer &text, const Game &game, int screenW, int screenH);

} // namespace cr
