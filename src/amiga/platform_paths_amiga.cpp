// The two hooks the shared asset layer expects a platform to provide (src/engine/platform_paths.h).
//
// NOT baseDir()/dataDir(): those are implemented once in engine/assets.cpp, which searches a list of candidates and
// calls down to these. Defining them here as well is what the first attempt did, and the linker said so - three
// "multiple definition" errors against engine_assets.o.
//
// PROGDIR: is AmigaOS's "the drawer this executable was started from", which is exactly right: the game is unpacked
// into one drawer and writes its config next to itself, never into the boot volume. It needs no trailing slash -
// "PROGDIR:" + "data/" is already a valid path - and the sibling ports made the same choice.
#include <string>

#include "engine/platform_paths.h"

namespace cr {

std::string platformBaseDir() { return "PROGDIR:"; }

// AmigaOS has environment variables (ENV:), but the game has no use for one and reading them would mean touching
// dos.library from C++ - which this port keeps out of C++ on purpose. nullptr means "unset", as on the SF2000.
const char *platformEnv(const char *) { return nullptr; }

} // namespace cr
