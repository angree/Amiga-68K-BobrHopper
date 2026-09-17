#include "platform_paths.h"

#include <SDL.h>

namespace cr {

std::string platformBaseDir()
{
    char *p = SDL_GetBasePath();
    std::string dir = p ? p : "";
    SDL_free(p);
    return dir;
}

const char *platformEnv(const char *name) { return SDL_getenv(name); }

} // namespace cr
