#include "log.h"

#include <cstdarg>
#include <cstdio>

namespace cr {

static FILE *s_file = nullptr;

void logOpen(const std::string &path)
{
    logClose();
    s_file = std::fopen(path.c_str(), "w");
}

void logClose()
{
    if (s_file) std::fclose(s_file);
    s_file = nullptr;
}

void logf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap); // global: no std::vsnprintf in the SF2000 toolchain's libstdc++
    va_end(ap);
    std::fputs(buf, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    if (s_file) {
        std::fputs(buf, s_file);
        std::fputc('\n', s_file);
        std::fflush(s_file);
    }
}

} // namespace cr
