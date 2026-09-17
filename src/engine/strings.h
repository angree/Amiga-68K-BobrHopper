// Integer to decimal text without std::to_string: the SF2000 toolchain's libstdc++ (mips-mti-elf GCC 7.4, newlib) is
// built without C99 stdio in namespace std, so std::to_string / std::snprintf do not exist there. Same text as
// std::to_string for every value.
#pragma once

#include <cstdio>
#include <string>

namespace cr {

inline std::string toString(long long value)
{
    char buf[24];
    snprintf(buf, sizeof buf, "%lld", value);
    return buf;
}

} // namespace cr
