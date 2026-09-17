// O11.5: every word the screens print, in English and Polish. Polish-speaking children play this at the user's home,
// so the whole UI switches language in the settings; the baked font carries the Polish letters (tools/bake_font.py).
// The language lives here rather than in UserSettings so the HUD and the screens read it without passing it around.
#pragma once

namespace cr {
namespace lang {

enum Str {
    Classic, Progression, Continue, NewGame, DeleteProgress, Yes, No,
    Level, Rank, NoRank, LevelDone, NewRank, TryAgain, NewBest, Score, Top,
    Paused, Resume, Settings, MenuItem, Exit, Back,
    Sounds, Music, Shadows, Full, Simple, Off, On, FpsCounter, View, Normal, Wide, Character, Language,
    HintHome, HintCareer, HintConfirm, HintPause, HintSettings, HintLevelOver,
    Count
};

// 0 English, 1 Polish
void set(int language);
int current();

const char *t(Str s);

} // namespace lang
} // namespace cr
