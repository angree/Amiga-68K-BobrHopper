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

// { English, Polish, Spanish, Latin } in the order of enum Str. Spanish and Latin (the Latin for fun, at the
// author's request) came later; the font draws capitals for every case, so case here is only for reading.
static const char *const kText[Count][4] = {
    {"CLASSIC", "KLASYCZNA", "CLÁSICO", "CLASSICUS"},
    {"PROGRESSION", "PROGRESJA", "PROGRESIÓN", "PROGRESSIO"},
    {"CONTINUE", "KONTYNUUJ", "CONTINUAR", "PERGE"},
    {"NEW GAME", "NOWA GRA", "NUEVA PARTIDA", "NOVUS LUDUS"},
    {"DELETE PROGRESS?", "SKASOWAĆ POSTĘP?", "¿BORRAR EL PROGRESO?", "PROGRESSUM DELERE?"},
    {"YES", "TAK", "SÍ", "ITA"},
    {"NO", "NIE", "NO", "NON"},
    {"LEVEL", "POZIOM", "NIVEL", "GRADUS"},
    {"RANK", "RANGA", "RANGO", "ORDO"},
    {"NO RANK YET", "BEZ RANGI", "SIN RANGO", "NULLUS ORDO"},
    {"LEVEL DONE", "POZIOM ZALICZONY", "NIVEL SUPERADO", "GRADUS PERACTUS"},
    {"NEW RANK", "NOWA RANGA", "NUEVO RANGO", "NOVUS ORDO"},
    {"TRY AGAIN", "SPRÓBUJ JESZCZE RAZ", "INTÉNTALO DE NUEVO", "ITERUM CONARE"},
    {"NEW BEST", "NOWY REKORD", "NUEVO RÉCORD", "NOVUM OPTIMUM"},
    {"SCORE", "WYNIK", "PUNTOS", "PUNCTA"},
    {"TOP", "REKORD", "RÉCORD", "SUMMUM"},
    {"PAUSED", "PAUZA", "PAUSA", "INTERMISSIO"},
    {"RESUME", "WRÓĆ DO GRY", "CONTINUAR", "REDI AD LUDUM"},
    {"SETTINGS", "USTAWIENIA", "AJUSTES", "OPTIONES"},
    {"MENU", "MENU", "MENÚ", "INDEX"},
    {"EXIT", "WYJŚCIE", "SALIR", "EXITUS"},
    {"BACK", "POWRÓT", "VOLVER", "REDI"},
    {"SOUNDS", "DŹWIĘKI", "SONIDOS", "SONI"},
    {"MUSIC", "MUZYKA", "MÚSICA", "MUSICA"},
    {"SHADOWS", "CIENIE", "SOMBRAS", "UMBRAE"},
    {"FULL", "PEŁNE", "COMPLETAS", "PLENAE"},
    {"SIMPLE", "PROSTE", "SIMPLES", "SIMPLICES"},
    {"OFF", "WYŁ", "NO", "NON"},
    {"ON", "WŁ", "SÍ", "ITA"},
    {"FPS COUNTER", "LICZNIK FPS", "CONTADOR FPS", "NUMERUS FPS"},
    {"VIEW", "WIDOK", "VISTA", "ASPECTUS"},
    {"NORMAL", "NORMALNY", "NORMAL", "NORMALIS"},
    {"WIDE", "SZEROKI", "AMPLIA", "LATUS"},
    {"CHARACTER", "POSTAĆ", "PERSONAJE", "PERSONA"},
    {"LANGUAGE", "JĘZYK", "IDIOMA", "LINGUA"},
    {"PLAYERS", "GRACZE", "JUGADORES", "LUSORES"},
    {"CONTROL P1", "STEROWANIE G1", "CONTROL J1", "IMPERIUM L1"},
    {"CONTROL P2", "STEROWANIE G2", "CONTROL J2", "IMPERIUM L2"},
    {"1 PLAYER", "1 GRACZ", "1 JUGADOR", "1 LUSOR"},
    {"2 PLAYERS", "2 GRACZE", "2 JUGADORES", "2 LUSORES"},
    {"WINS", "WYGRYWA", "GANA", "VINCIT"},
    {"DRAW", "REMIS", "EMPATE", "AEQUUM"},
    {"PLAYER 1", "GRACZ 1", "JUGADOR 1", "LUSOR 1"},
    {"PLAYER 2", "GRACZ 2", "JUGADOR 2", "LUSOR 2"},
    {"ASK", "PYTAJ", "PREGUNTAR", "ROGA"},
    {"HOW MANY PLAYERS?", "ILU GRACZY?", "¿CUÁNTOS JUGADORES?", "QUOT LUSORES?"},
    {"ENDLESS RETRIES", "BEZ KOŃCA PRÓB", "REINTENTOS SIN FIN", "CONATUS INFINITI"},
    {"A CHOOSE   B BACK", "A WYBIERZ   B POWRÓT", "A ELEGIR   B VOLVER", "A ELIGE   B REDI"},
    {"A START   SELECT SETTINGS", "A GRAJ   SELECT USTAWIENIA", "A JUGAR   SELECT AJUSTES", "A LUDE   SELECT OPTIONES"},
    {"A SELECT   B BACK", "A WYBIERZ   B POWRÓT", "A ELEGIR   B VOLVER", "A ELIGE   B REDI"},
    {"A CONFIRM   B CANCEL", "A POTWIERDŹ   B ANULUJ", "A CONFIRMAR   B CANCELAR", "A CONFIRMA   B ABROGA"},
    {"A SELECT   B RESUME", "A WYBIERZ   B WRÓĆ DO GRY", "A ELEGIR   B CONTINUAR", "A ELIGE   B REDI AD LUDUM"},
    {"LEFT RIGHT CHANGE   A SELECT   B BACK", "LEWO PRAWO ZMIEŃ   A WYBIERZ   B POWRÓT", "IZQ DER CAMBIAR   A ELEGIR   B VOLVER", "SINISTRA DEXTRA MUTA   A ELIGE   B REDI"},
    {"A CONTINUE   B MENU", "A KONTYNUUJ   B MENU", "A CONTINUAR   B MENÚ", "A PERGE   B INDEX"},
    {"SCREEN", "EKRAN", "PANTALLA", "TABULA"},
    {"FULL", "PEŁNY", "COMPLETA", "PLENA"},
    {"NARROW", "WĄSKI", "ESTRECHA", "ANGUSTA"},
    {"PHONE", "TELEFON", "TELÉFONO", "TELEPHONUM"},
    {"BEAVER", "BÓBR", "CASTOR", "CASTOR"},
    {"CHICKEN", "KURCZAK", "POLLO", "PULLUS"},
    {"BACON", "BEKON", "BEICON", "LARIDUM"},
    {"PLAY", "GRAJ", "JUGAR", "LUDE"},
    {"QUIT THE GAME?", "WYJŚĆ Z GRY?", "¿SALIR DEL JUEGO?", "LUDUM RELINQUERE?"},
    {"ENTER - YES     ESC - NO", "ENTER - TAK     ESC - NIE", "ENTER - SÍ     ESC - NO", "ENTER - ITA     ESC - NON"},
    {"NIGHT MODE", "TRYB NOCNY", "MODO NOCHE", "MODUS NOCTIS"},
};

static int g_language = 0;

void set(int language) { g_language = language >= 0 && language < kLanguages ? language : 0; }
int current() { return g_language; }

const char *t(Str s) { return s >= 0 && s < Count ? kText[s][g_language] : ""; }

} // namespace lang
} // namespace cr
