#include "png_write.h"

#include <cstdio>
#include <vector>

namespace png {

static uint32_t crcTable[256];
static bool crcReady = false;

static uint32_t crc(const uint8_t *data, size_t len, uint32_t c = 0xFFFFFFFFu)
{
    if (!crcReady) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t v = n;
            for (int k = 0; k < 8; k++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crcTable[n] = v;
        }
        crcReady = true;
    }
    for (size_t i = 0; i < len; i++)
        c = crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void putU32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

static void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data)
{
    putU32(out, uint32_t(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    putU32(out, crc(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

bool writeRGBA(const std::string &path, int width, int height, const uint8_t *rgba)
{
    std::vector<uint8_t> raw;
    raw.reserve(size_t(height) * (size_t(width) * 4 + 1));
    for (int y = 0; y < height; y++) {
        raw.push_back(0); // filter: none
        const uint8_t *row = rgba + size_t(y) * size_t(width) * 4;
        raw.insert(raw.end(), row, row + size_t(width) * 4);
    }

    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    const size_t maxBlock = 65535;
    for (size_t pos = 0; pos < raw.size() || raw.empty(); pos += maxBlock) {
        size_t len = raw.size() - pos < maxBlock ? raw.size() - pos : maxBlock;
        bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(len));
        z.push_back(uint8_t(len >> 8));
        z.push_back(uint8_t(~len));
        z.push_back(uint8_t(~len >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        if (raw.empty()) break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    putU32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    putU32(ihdr, uint32_t(width));
    putU32(ihdr, uint32_t(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

} // namespace png
