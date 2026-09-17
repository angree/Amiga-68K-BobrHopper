// O8: an easier start for the user's children ("u mnie w domu grają w to dzieci"). The game (GameContext::
// originalBehaviour = false) draws its rows and speeds as before, from smaller baskets that open as the score grows.
// O15: and a harder end. From 150 points the slowest layer is taken away in turn - first the slowest cars, then the
// slowest logs, then the safest spacing of railroads - and every 30 points after that the next one goes. Speeds stop
// at the lane's own maximum, but the smallest run of dangerous rows keeps growing, so the game never calms down.
// Every function takes the score a player has when stepping onto the row (Game::updateScore: row z - startingRow).
#pragma once

#include "game/settings.h"

namespace cr {
namespace difficulty {

inline int scoreOfRow(int z) { return z > settings::startingRow ? z - settings::startingRow : 0; }

// O15: how many times the bottom layer has been cut away at this score. The first three cuts come 10 points apart
// (150, 160, 170), then one more every 30 points: 200, 230, 260 ...
inline int cutsFromBelow(int score)
{
    if (score < 150) return 0;
    if (score < 160) return 1;
    if (score < 170) return 2;
    if (score < 200) return 3;
    return 3 + 1 + (score - 200) / 30;
}

// dangerous rows (roads, railroads, water with logs: standing there long enough kills) in a row: 1 below 10 points,
// 2 below 40, 3 below 100 ("do 70 3", 4 only "od 100"), then one more every 30 points
inline int dangerousRowsInARow(int score)
{
    if (score < 10) return 1;
    if (score < 40) return 2;
    if (score < 100) return 3;
    return 4 + (score - 100) / 30;
}

// O15: and how many there must be at least. Nothing below 150 points; then one more with every cut, kept under the
// maximum above so a row can still be grass. This one has no ceiling - it is what makes the late game relentless.
inline int dangerousRowsAtLeast(int score)
{
    const int want = cutsFromBelow(score);
    const int most = dangerousRowsInARow(score);
    return want < most ? want : most - 1;
}

// the car speeds (0.02 + 0.06 r) in four equal baskets: the slowest alone below 20 points, then one more every 30
inline int carSpeedBaskets(int score) { return score < 20 ? 1 : score < 50 ? 2 : score < 80 ? 3 : 4; }

// O15: the first basket still in use. The slowest cars go at the first cut (150 points), the next at the third
// (170), and so on, but three of the four baskets always remain - the fastest lane is the fastest lane.
inline int carSpeedFirstBasket(int score)
{
    const int cuts = cutsFromBelow(score);
    const int gone = cuts >= 5 ? 3 : cuts >= 3 ? 2 : cuts >= 1 ? 1 : 0;
    return gone > 3 ? 3 : gone;
}

// the log speeds (0.02 + 0.05 r) in four equal baskets: one more after 15, 45 and 75 points
inline int logSpeedBaskets(int score) { return score < 15 ? 1 : score < 45 ? 2 : score < 75 ? 3 : 4; }

// O15: the slowest logs go one cut later than the cars (160 points), then again at 200 and 260
inline int logSpeedFirstBasket(int score)
{
    const int cuts = cutsFromBelow(score);
    const int gone = cuts >= 6 ? 3 : cuts >= 4 ? 2 : cuts >= 2 ? 1 : 0;
    return gone > 3 ? 3 : gone;
}

// railroads among a row and the 9 before it: 1 below 10 points, 2 below 20, ...
inline int railroadsPerTenRows(int score) { return 1 + score / 10; }

// O15: and how many there must be at least, from the third cut (170 points) on - the quiet stretches without a
// single track disappear. Kept under the maximum above.
inline int railroadsAtLeastPerTenRows(int score)
{
    const int cuts = cutsFromBelow(score);
    int want = cuts < 3 ? 0 : 1 + (cuts - 3) / 2;
    if (want > 5) want = 5; // half the rows at most: ten rows cannot hold more, and all-track is not a game
    const int most = railroadsPerTenRows(score);
    return want < most ? want : most - 1;
}

} // namespace difficulty
} // namespace cr
