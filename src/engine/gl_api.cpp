#include "gl_api.h"

#include <SDL.h>

#define CR_GL_DEFINE(ret, name, args) ret (GLAPIENTRY *name) args = nullptr;
CR_GL_FUNCS(CR_GL_DEFINE)
#undef CR_GL_DEFINE

namespace gl {

static bool s_desktop = false;

bool load(const char **missingName)
{
#define CR_GL_LOAD(ret, name, args)                                                  \
    name = reinterpret_cast<ret (GLAPIENTRY *) args>(SDL_GL_GetProcAddress(#name));  \
    if (!name) {                                                                     \
        if (missingName) *missingName = #name;                                       \
        return false;                                                                \
    }
    CR_GL_FUNCS(CR_GL_LOAD)
#undef CR_GL_LOAD
    return true;
}

bool isDesktop() { return s_desktop; }
void setDesktop(bool desktop) { s_desktop = desktop; }

const char *shaderPreamble()
{
    if (s_desktop)
        return "#version 120\n#define lowp\n#define mediump\n#define highp\n";
    return "#version 100\nprecision mediump float;\n";
}

} // namespace gl
