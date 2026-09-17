# Notice

Both copyright holders in [LICENSE](LICENSE) release their work under the same MIT licence. This file says which
part is whose, and what is somebody else's.

**Evan Bacon** - [expo-crossy-road](https://github.com/EvanBacon/expo-crossy-road), the game this port is derived
from: its structure and its logic. The models the sprites were baked from come from it.

**G. Korycki** - this Amiga port: the C and C++ sources, the AmigaOS display, Paula audio and ADPCM streaming layers
(first written for the author's native AmigaOS port of OpenTTD, and the author's own code), the sprite baker, the
data tools, the beaver, the logo, the icon, the soundtrack and every sound effect.

## Third-party code

- `src/amiga/c2p_rect.s`, `src/amiga/c2p1x1_6_c5_bm_040.s` - chunky-to-planar routines by **Mikael Kalms**
  ([kalms-c2p](https://github.com/Kalmalyzer/kalms-c2p)), used as published.
- **Not included:** the CyberGraphX developer headers the RTG code is compiled against (`src/amiga/cgx-include/`).
  They are phase5's and not redistributable; see README.md.

## Audio

No audio from the original project is included. Every sound and music track was made for this port.

## Trademarks

This project is not affiliated with, endorsed by, or connected to Hipster Whale, Yodo1, or the "Crossy Road" game
or trademark.
