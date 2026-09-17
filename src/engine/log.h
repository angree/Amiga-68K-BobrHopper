// Line log to stdout and a file, flushed per line so a crash on the device still leaves a full log.
#pragma once

#include <string>

namespace cr {

void logOpen(const std::string &path);
void logClose();

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void logf(const char *fmt, ...);

} // namespace cr
