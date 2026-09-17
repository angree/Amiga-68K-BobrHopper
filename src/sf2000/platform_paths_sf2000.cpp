// SF2000: no environment and no executable path; the core sets the base directory from the ROM path (setBaseDir).
#include "engine/platform_paths.h"

namespace cr {

std::string platformBaseDir() { return std::string(); }

const char *platformEnv(const char *) { return nullptr; }

} // namespace cr
