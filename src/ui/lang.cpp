#include "ui/lang.h"

#include "engine/strings.h"
#include "ui/ranks.h"

namespace cr {

// the level's name, the same in both languages: five levels to a world (1-1 .. 1-5, 2-1 ...)
std::string levelLabel(int level)
{
    const int n = level > 0 ? level - 1 : 0;
    return toString(n / 5 + 1) + "-" + toString(n % 5 + 1);
}

namespace lang {

// { English, Polish } in the order of enum Str. The font draws capitals for both cases, so the case here is only
// for reading the table.
static const char *const kText[Count][2] = {
    {"CLASSIC", "KLASYCZNA"},
    {"PROGRESSION", "PROGRESJA"},
    {"CONTINUE", "KONTYNUUJ"},
    {"NEW GAME", "NOWA GRA"},
    {"DELETE PROGRESS?", "SKASOWAĆ POSTĘP?"},
    {"YES", "TAK"},
    {"NO", "NIE"},
    {"LEVEL", "POZIOM"},
    {"RANK", "RANGA"},
    {"NO RANK YET", "BEZ RANGI"},
    {"LEVEL DONE", "POZIOM ZALICZONY"},
    {"NEW RANK", "NOWA RANGA"},
    {"TRY AGAIN", "SPRÓBUJ JESZCZE RAZ"},
    {"NEW BEST", "NOWY REKORD"},
    {"SCORE", "WYNIK"},
    {"TOP", "REKORD"},
    {"PAUSED", "PAUZA"},
    {"RESUME", "WRÓĆ DO GRY"},
    {"SETTINGS", "USTAWIENIA"},
    {"MENU", "MENU"},
    {"EXIT", "WYJŚCIE"},
    {"BACK", "POWRÓT"},
    {"SOUNDS", "DŹWIĘKI"},
    {"MUSIC", "MUZYKA"},
    {"SHADOWS", "CIENIE"},
    {"FULL", "PEŁNE"},
    {"SIMPLE", "PROSTE"},
    {"OFF", "WYŁ"},
    {"ON", "WŁ"},
    {"FPS COUNTER", "LICZNIK FPS"},
    {"VIEW", "WIDOK"},
    {"NORMAL", "NORMALNY"},
    {"WIDE", "SZEROKI"},
    {"CHARACTER", "POSTAĆ"},
    {"LANGUAGE", "JĘZYK"},
    {"PLAYERS", "GRACZE"},
    {"CONTROL P1", "STEROWANIE G1"},
    {"CONTROL P2", "STEROWANIE G2"},
    {"1 PLAYER", "1 GRACZ"},
    {"2 PLAYERS", "2 GRACZE"},
    {"WINS", "WYGRYWA"},
    {"DRAW", "REMIS"},
    {"PLAYER 1", "GRACZ 1"},
    {"PLAYER 2", "GRACZ 2"},
    {"A START   SELECT SETTINGS", "A GRAJ   SELECT USTAWIENIA"},
    {"A SELECT   B BACK", "A WYBIERZ   B POWRÓT"},
    {"A CONFIRM   B CANCEL", "A POTWIERDŹ   B ANULUJ"},
    {"A SELECT   B RESUME", "A WYBIERZ   B WRÓĆ DO GRY"},
    {"LEFT RIGHT CHANGE   A SELECT   B BACK", "LEWO PRAWO ZMIEŃ   A WYBIERZ   B POWRÓT"},
    {"A CONTINUE   B MENU", "A KONTYNUUJ   B MENU"},
};

static int g_language = 0;

void set(int language) { g_language = language == 1 ? 1 : 0; }
int current() { return g_language; }

const char *t(Str s) { return s >= 0 && s < Count ? kText[s][g_language] : ""; }

} // namespace lang
} // namespace cr
