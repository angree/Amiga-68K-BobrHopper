// src/GameSettings.ts, 1:1, plus the port's own framing constants.
#pragma once

#include "engine/math.h"

namespace cr {
namespace settings {

constexpr real groundLevel = 0.4;
constexpr unsigned sceneColor = 0x87C6FF;
constexpr int startingRow = 8;
constexpr int maxRows = 20;
constexpr bool disableDriftwood = false;
constexpr real cameraEasing = 0.03;
constexpr real mapOffset = -30;
// double, like the JS number: GSAP compares timeline times against it (0.1f starts segments a tick late)
constexpr real baseAnimationTime = 0.1;
constexpr bool idleDuringGamePlay = false;
constexpr real PI_2 = 3.141592653589793 * 0.5; // Math.PI * 0.5, not the float PI
constexpr real playerIdleScale = 0.8;
constexpr real heroWidth = 0.7;
// O23 (two players): how high one player stands when it lands on the other's head. Every character is scaled so
// that its longest side is 1 unit, so a hero is a little under a unit tall; this is measured to sit the upper one
// on the lower one's head rather than in it.
constexpr real headHeight = 0.75;
// O23 Progression 2P: steps before a dead player is put back into the game on the partner's head (2 s at 60 Hz)
constexpr int respawnSteps = 120;

// CrossyCamera: OrthographicCamera(-1, 1, 1, -1, -30, 30), lookAt(0,0,0) from (-1, 2.8, -2.9),
// then init() sets position.z = 1 (the rotation stays from the constructor)
constexpr real cameraNear = -30, cameraFar = 30;
constexpr real cameraZoom = 400;

// Light: DirectionalLight at (20, 30, 0.05) aimed at the origin; AmbientLight 1.8
constexpr real lightX = 20, lightY = 30, lightZ = 0.05;

// Port framing for the landscape 640x480 screen (task K.1, numbers in docs/PROGRESS.md). The original sizes
// its view from width * devicePixelRatio; scale 2 is what it shows at 640x480 DPR 2, but in landscape the
// hero's feet leave the bottom edge in 8% of live play. Scale 3 with the picture raised by 0.15 NDC keeps the
// hero at 58-87% of the screen height (median 71%), shows 8 rows ahead and 2 behind, chicken 47 px tall.
constexpr real defaultViewScale = 3.0;
// vertical picture shift in NDC (SceneRenderer::viewShift): positive puts the hero lower, negative higher
constexpr real defaultViewShift = -0.15;

} // namespace settings
} // namespace cr
