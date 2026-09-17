// Minimal PNG writer (stored deflate blocks, no compression) for --shot screenshots.
#pragma once

#include <cstdint>
#include <string>

namespace png {

// rgba: width*height*4 bytes, first row is the TOP of the image.
bool writeRGBA(const std::string &path, int width, int height, const uint8_t *rgba);

} // namespace png
