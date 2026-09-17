#include "ui/screens.h"

#include <algorithm>
#include <cmath>

#include "engine/assets.h"
#include "engine/log.h"
#include "game/game.h"
#include "engine/strings.h"
#include "ui/easing.h"
#include "ui/lang.h"
#include "ui/ranks.h"

namespace cr {

// real/mreal (engine/real.h): double/float on PC and R36S, 16.16 on the SF2000; every expression keeps the operation
// order of the double version
static const real kStep = 1.0 / 60.0;

static bool loadImage(Renderer &renderer, const std::string &path, GpuTexture &out)
{
    TextureData data;
    if (!loadTexture(path, data)) {
        logf("screens: cannot load %s", path.c_str());
        return false;
    }
    out = renderer.uploadTexture(data);
    return true;
}

bool Screens::load(Renderer &renderer, const std::string &dataDir)
{
    return loadImage(renderer, dataDir + "images/title.tex", title_) &&
           loadImage(renderer, dataDir + "images/button_long_play.tex", buttonPlay_) &&
           loadImage(renderer, dataDir + "images/button_settings.tex", buttonSettings_) &&
           loadImage(renderer, dataDir + "images/button_back.tex", buttonBack_);
}

void Screens::openPause()
{
    menu_ = Menu::Pause;
    cursor_ = 0;
}

void Screens::openSettings(bool fromPause)
{
    menu_ = Menu::Settings;
    settingsFromPause_ = fromPause;
    cursor_ = 0;
}

// O11.2: the home screen owns Up / Down / A / B; everything else (Select for the settings) stays with the app
bool Screens::handleHome(const Input &in, MenuResult &out)
{
    auto sound = [this](const char *name) {
        if (playSound) playSound(name);
    };
    bool used = false;
    if (in.pressed(ActA) || in.pressed(ActB)) {
        sound("button_in");
        used = true;
    }
    if (in.pressed(ActUp) || in.pressed(ActDown)) {
        int &cursor = homePage_ == HomePage::Modes    ? homeCursor_
                      : homePage_ == HomePage::Career ? careerCursor_
                                                      : confirmCursor_;
        cursor = 1 - cursor; // every page has two items
        used = true;
    }
    if (in.released(ActB)) {
        used = true;
        if (homePage_ != HomePage::Modes) {
            sound("button_out");
            homePage_ = homePage_ == HomePage::Confirm ? HomePage::Career : HomePage::Modes;
            pageTime_ = 0;
        }
    }
    if (in.released(ActA)) {
        sound("button_out");
        used = true;
        switch (homePage_) {
        case HomePage::Modes:
            if (homeCursor_ == 0) {
                out.startLevel = 0; // Classic: the endless game
            } else {
                homePage_ = HomePage::Career;
                careerCursor_ = 0;
                pageTime_ = 0;
            }
            break;
        case HomePage::Career:
            if (careerCursor_ == 0) {
                out.startLevel = careerLevel; // Continue
            } else {
                homePage_ = HomePage::Confirm;
                confirmCursor_ = 0;
                pageTime_ = 0;
            }
            break;
        case HomePage::Confirm:
            if (confirmCursor_ == 0) { // No
                homePage_ = HomePage::Career;
                pageTime_ = 0;
            } else { // Yes: the saved progress goes and the career starts at 1-1
                out.resetCareer = true;
                out.startLevel = 1;
            }
            break;
        }
    }
    return used;
}

bool Screens::handleInput(const Input &in, UserSettings &s, MenuResult &out)
{
    auto sound = [this](const char *name) {
        if (playSound) playSound(name);
    };
    if (menu_ == Menu::None) {
        if (lastState_ != int(GameState::None)) return false;
        return handleHome(in, out);
    }
    // Button: button_in when pressed, button_out when released
    if (in.pressed(ActA) || in.pressed(ActB)) sound("button_in");
    const int count = menu_ == Menu::Pause ? 4 : 8;
    if (in.pressed(ActUp)) cursor_ = (cursor_ + count - 1) % count;
    if (in.pressed(ActDown)) cursor_ = (cursor_ + 1) % count;

    if (menu_ == Menu::Pause) {
        if ((in.pressed(ActStart) && !in.down(ActSelect)) || in.released(ActB)) {
            if (in.released(ActB)) sound("button_out");
            menu_ = Menu::None;
            out.resume = true;
        } else if (in.released(ActA)) {
            sound("button_out");
            switch (cursor_) {
            case 0: menu_ = Menu::None; out.resume = true; break;
            case 1: openSettings(true); break;
            case 2: menu_ = Menu::None; out.quitToHome = true; break;
            default: out.exitGame = true; break;
            }
        }
        return true;
    }

    auto back = [&]() {
        if (settingsFromPause_) {
            menu_ = Menu::Pause;
            cursor_ = 1;
        } else {
            menu_ = Menu::None;
        }
    };
    if (in.released(ActB)) {
        sound("button_out");
        back();
        return true;
    }
    const bool activate = in.released(ActA);
    int dir = in.pressed(ActRight) ? 1 : in.pressed(ActLeft) ? -1 : 0;
    if (activate && dir == 0) dir = 1;
    if (dir == 0) return true;
    bool changed = true;
    switch (cursor_) {
    case 0: {
        const int v = std::max(0, std::min(10, s.volume + dir));
        changed = v != s.volume;
        s.volume = v;
        break;
    }
    case 1: {
        const int v = std::max(0, std::min(100, s.music + 2 * dir));
        changed = v != s.music;
        s.music = v;
        break;
    }
    case 2: s.shadows = (s.shadows + dir + 3) % 3; break;
    case 3: s.fpsCounter = !s.fpsCounter; break;
    case 4: s.framing = 1 - s.framing; break;
    // O11.5: the language of the whole UI, in place of the battery saver the user asked to drop
    case 5:
        s.language = 1 - s.language;
        lang::set(s.language);
        break;
    case 6: s.character = (s.character + dir + kCharacterCount) % kCharacterCount; break;
    default:
        changed = false;
        if (activate) {
            sound("button_out");
            back();
        }
        break;
    }
    if (changed) {
        sound("button_out");
        out.settingsChanged = true;
    }
    return true;
}

void Screens::update(const Game &game)
{
    const int state = int(game.state());
    pageTime_ += kStep;
    if (fadeTime_ >= real(0)) {
        fadeTime_ += kStep;
        if (fadeTime_ > real(0.5)) fadeTime_ = real(-1);
    }
    if (state != lastState_) {
        const int previous = lastState_;
        lastState_ = state;
        stateTime_ = 0;
        if (game.state() == GameState::None) {
            homeVisits_++;
            // O11.2: back on the home screen the menu starts at the modes again (the last mode stays selected)
            homePage_ = HomePage::Modes;
            pageTime_ = 0;
            if (previous == int(GameState::GameOver) || previous == int(GameState::Playing)) fadeTime_ = 0;
        }
        if (game.state() == GameState::Playing) bestAtStart_ = game.highscore();
        return;
    }
    const real before = stateTime_;
    stateTime_ += kStep;
    if (game.state() == GameState::GameOver && playSound) {
        // setTimeout 600 ms: animate the banners and play `banner`, again after 300 and 600 ms
        for (real at : {real(0.6), real(0.9), real(1.2)})
            if (before < at && stateTime_ >= at) playSound("banner");
    }
}

void Screens::draw(Renderer &renderer, TextRenderer &text, const Game &game, int screenW, int screenH)
{
    renderer.beginOverlay(screenW, screenH);
    if (game.state() == GameState::None) drawHome(renderer, text, screenW, screenH);
    if (game.state() == GameState::GameOver) drawGameOver(renderer, text, game, screenW, screenH);
    if (menu_ == Menu::Pause) drawPause(renderer, text, screenW, screenH);
    if (menu_ == Menu::Settings) drawSettings(renderer, text, screenW, screenH);
    renderer.endOverlay();
}

static const Rgba kWhite{1, 1, 1, 1}, kBlack{0, 0, 0, 1};
static const Rgba kYellow{0xF8 / 255.0f, 0xE8 / 255.0f, 0x4D / 255.0f, 1}; // HomeScreen coins colour

static void centred(Renderer &renderer, TextRenderer &text, const std::string &s, int w, int y, int size, Rgba color,
                    int outline)
{
    text.drawOutlined(renderer, s, (w - text.width(s, size)) / 2, y, size, color, outline, kBlack);
}

// index.tsx isPaused overlay / SettingsScreen container: rgba(105, 201, 230, 0.8)
static void menuBackground(Renderer &renderer, int w, int h)
{
    renderer.drawOverlayRect(0, 0, mreal(w), mreal(h), 105 / 255.0f, 201 / 255.0f, 230 / 255.0f, 0.8f);
}

void Screens::drawPause(Renderer &renderer, TextRenderer &text, int w, int h)
{
    menuBackground(renderer, w, h);
    centred(renderer, text, lang::t(lang::Paused), w, 96, 32, kWhite, 3);
    const lang::Str items[4] = {lang::Resume, lang::Settings, lang::MenuItem, lang::Exit};
    for (int i = 0; i < 4; i++)
        centred(renderer, text, lang::t(items[i]), w, 180 + i * 44, 18, i == cursor_ ? kYellow : kWhite, 2);
    centred(renderer, text, lang::t(lang::HintPause), w, h - 30, 12, kWhite, 2);
}

void Screens::drawSettings(Renderer &renderer, TextRenderer &text, int w, int h)
{
    static const UserSettings defaults;
    const UserSettings &s = settings ? *settings : defaults;
    menuBackground(renderer, w, h);
    // SettingsScreen: back button (60x48 image box, contain) top-left
    renderer.drawOverlayImage(buttonBack_, 14, 8, 48, 48);
    text.drawOutlined(renderer, "B", 14 + (48 - text.width("B", 12)) / 2, 60, 12, kWhite, 2, kBlack);
    centred(renderer, text, lang::t(lang::Settings), w, 40, 32, kWhite, 3);

    static const lang::Str shadowNames[] = {lang::Full, lang::Simple, lang::Off};
    const lang::Str labelIds[7] = {lang::Sounds, lang::Music,    lang::Shadows,  lang::FpsCounter,
                                   lang::View,   lang::Language, lang::Character};
    const std::string values[7] = {toString(s.volume),
                                   toString(s.music),
                                   lang::t(shadowNames[std::max(0, std::min(2, s.shadows))]),
                                   lang::t(s.fpsCounter ? lang::On : lang::Off),
                                   lang::t(s.framing ? lang::Wide : lang::Normal),
                                   s.language ? "POLSKI" : "ENGLISH",
                                   kCharacters[std::max(0, std::min(kCharacterCount - 1, s.character))].name};
    const int left = 96, right = w - 96, size = 18;
    for (int i = 0; i < 7; i++) {
        const int y = 96 + i * 42;
        const Rgba c = i == cursor_ ? kYellow : kWhite;
        text.drawOutlined(renderer, lang::t(labelIds[i]), left, y, size, c, 2, kBlack);
        text.drawOutlined(renderer, values[i], right - text.width(values[i], size), y, size, c, 2, kBlack);
    }
    centred(renderer, text, lang::t(lang::Back), w, 96 + 7 * 42, size, cursor_ == 7 ? kYellow : kWhite, 2);
    centred(renderer, text, lang::t(lang::HintSettings), w, h - 30, 12, kWhite, 2);
}

void Screens::drawSceneFade(Renderer &renderer, int w, int h)
{
    if (fadeTime_ < real(0)) return;
    // Animated.timing to 0 (200 ms) then back to 1 (300 ms), default easing inOut(ease); the view's opacity
    // reveals the #87C6FF background behind it
    const real opacity = fadeTime_ < real(0.2) ? real(1) - easing::inOutEase(fadeTime_ / real(0.2))
                                               : easing::inOutEase(std::min(real(1.0), (fadeTime_ - real(0.2)) / real(0.3)));
    renderer.beginOverlay(w, h);
    renderer.drawOverlayRect(0, 0, mreal(w), mreal(h), 0x87 / 255.0f, 0xC6 / 255.0f, 0xFF / 255.0f,
                             mreal(real(1) - opacity));
    renderer.endOverlay();
}

// GameOverScreen.tsx + GameOver/Banner.tsx + GameOver/Footer.tsx
void Screens::drawGameOver(Renderer &renderer, TextRenderer &text, const Game &game, int w, int h)
{
    const Rgba white{1, 1, 1, 1}, black{0, 0, 0, 1};
    const int footerH = 56, padTop = 12, padBottom = 8, bannerH = 56, margin = 8;
    // content (flex 1, justifyContent center) holds the banners; the footer sits under it
    const int contentTop = padTop, contentH = h - padTop - padBottom - footerH;
    const int blockH = 3 * (bannerH + 2 * margin);
    const int blockTop = contentTop + (contentH - blockH) / 2;

    // port: results instead of the web version's mailing-list / gift / coins adverts, same colours
    static const mreal colors[3][3] = {
        {0x36 / 255.0f, 0x40 / 255.0f, 0xEB / 255.0f}, {0x36 / 255.0f, 0x8F / 255.0f, 0xEB / 255.0f},
        {0x36 / 255.0f, 0xD6 / 255.0f, 0xEB / 255.0f}};
    std::string titles[3];
    if (game.level() > 0) {
        // O11.3: which level it was, how far it got, and the rank a finished level carries
        const int total = game.levelRows();
        const int passed = std::min(game.score(), total);
        titles[0] = std::string(lang::t(lang::Level)) + " " + levelLabel(game.level());
        titles[1] = game.levelDone() ? lang::t(lang::LevelDone) : toString(passed) + "/" + toString(total);
        titles[2] = game.levelDone() ? std::string(lang::t(lang::NewRank)) + " : " + rankName(game.level())
                                     : lang::t(lang::TryAgain);
    } else {
        const bool newBest = game.score() > bestAtStart_;
        titles[0] = std::string(lang::t(lang::Score)) + " " + toString(game.score());
        titles[1] = std::string(lang::t(lang::Top)) + " " + toString(game.highscore());
        titles[2] = lang::t(newBest ? lang::NewBest : lang::TryAgain);
    }
    for (int i = 0; i < 3; i++) {
        // Animated.stagger(300) of timing(0 -> 1, 1000 ms, Easing.elastic()) started 600 ms after mounting
        const real t = (stateTime_ - real(0.6) - real(0.3) * real(i)) / real(1.0);
        const real v = t <= real(0) ? real(0) : easing::elastic(std::min(real(1.0), t));
        const real scaleY = std::max(real(0.0), std::min(real(1.0), v / real(0.2)));              // [0, 0.2] -> [0, 1]
        const real tx = std::min(real(0.0), real(-w) + (v - real(0.2)) / real(0.8) * real(w)); // [0.2, 1] -> [-w, 0]
        if (scaleY <= real(0)) continue;
        const real top = real(blockTop + margin + i * (bannerH + 2 * margin));
        const real hh = real(bannerH) * scaleY;
        renderer.drawOverlayRect(0, mreal(top + (real(bannerH) - hh) / real(2)), mreal(w), mreal(hh), colors[i][0],
                                 colors[i][1], colors[i][2], 1);
        if (v > real(0.2)) {
            // O11.9: outlined like every other text on the screens (the user found the plain ones hard to read)
            const int size = 18, tw = text.width(titles[i], size);
            const int shift = int(tx < real(-w) ? real(-w) : tx);
            const std::string::size_type colon = titles[i].find(" : ");
            if (tw > w - 24 && colon != std::string::npos) {
                // TOO LONG FOR THE BANNER ("NEW RANK : PROFESSIONAL TREE HUGGER" - user report on the Amiga):
                // two lines in a smaller face, the label above the name, in the same banner
                const std::string first = titles[i].substr(0, colon), second = titles[i].substr(colon + 3);
                const int small = text.width(second, 14) > w - 24 ? 12 : 14, lh = text.lineHeight(small);
                const int y0 = int(top) + (bannerH - 2 * lh - 2) / 2;
                text.drawOutlined(renderer, first, (w - text.width(first, small)) / 2 + shift, y0, small, white, 2, black);
                text.drawOutlined(renderer, second, (w - text.width(second, small)) / 2 + shift, y0 + lh + 2, small, white,
                                  2, black);
            } else {
                text.drawOutlined(renderer, titles[i], int(real((w - tw) / 2) + real(shift)),
                                  int(top + real((bannerH - text.lineHeight(size)) / 2)), size, white, 2, black);
            }
        }
    }

    // footer: the original's settings / share / play / leaderboard row; the console keeps the two that work,
    // labelled with their buttons
    const int footerTop = h - padBottom - footerH;
    const mreal settingsW = mreal(footerH) * mreal(1.25f), playW = mreal(footerH) * mreal(1.9f);
    renderer.drawOverlayImage(buttonSettings_, 8, mreal(footerTop), settingsW, mreal(footerH));
    renderer.drawOverlayImage(buttonPlay_, mreal(w - 8) - playW, mreal(footerTop), playW, mreal(footerH));
    const int labelSize = 12;
    // O11.9: in Progression A carries on with the career and B goes back to the menu, so the screen says so - right
    // under the banners, where the checkerboard of the finish line does not swallow it
    if (game.level() > 0)
        centred(renderer, text, lang::t(lang::HintLevelOver), w, blockTop + blockH + 10, 14, white, 2);
    text.drawOutlined(renderer, "SELECT", 8 + int(settingsW - mreal(text.width("SELECT", labelSize))) / 2,
                      footerTop - 18, labelSize, white, 2, black);
    text.drawOutlined(renderer, "A", int(mreal(w - 8) - playW + (playW - mreal(text.width("A", labelSize))) / mreal(2)),
                      footerTop - 18, labelSize, white, 2, black);
}

// O11.2: the game over screen's banners as menu items — the same bars, their blue pushed a little towards purple
// so the two screens do not look alike
void Screens::drawMenuBars(Renderer &renderer, TextRenderer &text, const std::string *labels, int count, int cursor,
                           int w, int top)
{
    static const mreal colors[2][3] = {{0x6A / 255.0f, 0x40 / 255.0f, 0xEB / 255.0f},
                                       {0x6A / 255.0f, 0x8F / 255.0f, 0xEB / 255.0f}};
    const int barH = 48, gap = 14, size = 18;
    for (int i = 0; i < count; i++) {
        // the banners' elastic entry, one bar after the other
        const real t = (pageTime_ - real(0.15) * real(i)) / real(0.8);
        const real v = t <= real(0) ? real(0) : easing::elastic(std::min(real(1.0), t));
        const real scaleY = std::max(real(0.0), std::min(real(1.0), v / real(0.2)));
        if (scaleY <= real(0)) continue;
        const real barTop = real(top + i * (barH + gap));
        const real hh = real(barH) * scaleY;
        const mreal *c = colors[i % 2];
        renderer.drawOverlayRect(0, mreal(barTop + (real(barH) - hh) / real(2)), mreal(w), mreal(hh), c[0], c[1], c[2],
                                 1);
        if (v > real(0.2))
            centred(renderer, text, labels[i], w, int(barTop) + (barH - text.lineHeight(size)) / 2, size,
                    i == cursor ? kYellow : kWhite, 2);
    }
}

// HomeScreen.tsx, plus the port's mode menu (O11.2)
void Screens::drawHome(Renderer &renderer, TextRenderer &text, int w, int h)
{
    // the original animates the title in on the first mount only; the user wants the logo back after every game,
    // so it slides in on every visit, above the menu instead of in the middle of the screen
    if (title_.width > 0) {
        const real p = easing::inOutEase(std::min(real(1.0), stateTime_ / real(0.8)));
        const real boxW = std::min(real(600.0), real(0.8) * real(w)), boxH = real(200.0);
        const real scale = std::min(boxW / real(title_.width), boxH / real(title_.height));
        const real iw = real(title_.width) * scale, ih = real(title_.height) * scale;
        const real x = (real(w) - iw) / real(2) - real(w) * (real(1) - p);
        const real y = real(30) - real(60) * (real(1) - p);
        renderer.drawOverlayImage(title_, mreal(x), mreal(y), mreal(iw), mreal(ih));
    }

    const int barsTop = 300;
    if (homePage_ == HomePage::Modes) {
        // O11.9: the rank earned so far belongs on the first screen too, not only behind Progression
        if (careerLevel > 1)
            centred(renderer, text, std::string(lang::t(lang::Rank)) + " : " + rankName(careerLevel - 1), w,
                    barsTop - 30, 14, kYellow, 2);
        const std::string labels[2] = {lang::t(lang::Classic), lang::t(lang::Progression)};
        drawMenuBars(renderer, text, labels, 2, homeCursor_, w, barsTop);
        // above the credit line in the corner, which the hint used to run into
        centred(renderer, text, lang::t(lang::HintHome), w, h - 48, 12, kWhite, 2);
    } else if (homePage_ == HomePage::Career) {
        // O11.4: where the career stands — the level Continue starts and the rank the last finished level gave
        centred(renderer, text, std::string(lang::t(lang::Level)) + " " + levelLabel(careerLevel), w, barsTop - 56, 18,
                kWhite, 2);
        const std::string rank = careerLevel > 1 ? std::string(lang::t(lang::Rank)) + " : " + rankName(careerLevel - 1)
                                                 : std::string(lang::t(lang::NoRank));
        centred(renderer, text, rank, w, barsTop - 30, 14, kYellow, 2);
        const std::string labels[2] = {lang::t(lang::Continue), lang::t(lang::NewGame)};
        drawMenuBars(renderer, text, labels, 2, careerCursor_, w, barsTop);
        centred(renderer, text, lang::t(lang::HintCareer), w, h - 48, 12, kWhite, 2);
    } else {
        centred(renderer, text, lang::t(lang::DeleteProgress), w, barsTop - 44, 18, kWhite, 2);
        const std::string labels[2] = {lang::t(lang::No), lang::t(lang::Yes)};
        drawMenuBars(renderer, text, labels, 2, confirmCursor_, w, barsTop);
        centred(renderer, text, lang::t(lang::HintConfirm), w, h - 48, 12, kWhite, 2);
    }

    const int creditSize = 12;
    text.drawOutlined(renderer, "PORT BY G. KORYCKI", 8, h - 8 - text.lineHeight(creditSize), creditSize, kWhite, 2,
                      kBlack);
    if (!versionLabel.empty()) {
        const int size = 12;
        text.drawOutlined(renderer, versionLabel, w - 8 - text.width(versionLabel, size), h - 8 - text.lineHeight(size),
                          size, kWhite, 2, kBlack);
    }
}

} // namespace cr
