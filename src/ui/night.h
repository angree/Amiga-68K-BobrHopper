// Night mode (UserSettings::night): the colour of a lamp at 4200 / 2700 / 1900 K against daylight (6500 K), per
// channel out of 256 - Tanner Helland's fit of the black-body curve. One table for every platform: the Amiga
// multiplies its palette registers by it, the consoles multiply the finished frame (Renderer::tintScreen).
#pragma once

namespace cr {

struct NightTint {
    short r, g, b; // 256 = unchanged
};

inline NightTint nightTint(int level)
{
    static const NightTint kTint[4] = {{256, 256, 256}, {256, 211, 176}, {256, 167, 88}, {256, 132, 0}};
    return kTint[level >= 0 && level <= 3 ? level : 0];
}

} // namespace cr
