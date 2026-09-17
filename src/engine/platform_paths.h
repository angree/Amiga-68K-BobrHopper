// The two things the shared engine code asks the operating system for: where the program lives and an environment
// variable. SDL builds (PC, R36S) implement them in platform_paths_sdl.cpp; the SF2000 core has no environment and
// learns its folder from the ROM path (setBaseDir in assets.h).
#pragma once

#include <string>

namespace cr {

// directory of the running executable with a trailing separator, or "" when the platform cannot tell
std::string platformBaseDir();

// environment variable, or nullptr when unset or unsupported
const char *platformEnv(const char *name);

} // namespace cr
