// The Ogg Vorbis decoder for the music streams: third_party/stb_vorbis.c (v1.22, public domain, nothings/stb commit
// 2c980bb59875b0d32144a71867fbdebb2f77cd20), compiled once here. Declarations for users: engine/stb_vorbis.h.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_VORBIS_NO_STDIO
#include "../../third_party/stb_vorbis.c"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
