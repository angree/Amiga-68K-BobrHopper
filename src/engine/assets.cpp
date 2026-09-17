#include "assets.h"

#include <cstdio>
#include <cstring>

#include "platform_paths.h"

namespace cr {

static std::string &baseDirStorage()
{
    static std::string dir;
    return dir;
}

void setBaseDir(const std::string &dir)
{
    baseDirStorage() = dir.empty() || dir.back() == '/' || dir.back() == '\\' ? dir : dir + "/";
}

const std::string &baseDir()
{
    std::string &dir = baseDirStorage();
    if (dir.empty()) {
        dir = platformBaseDir();
        if (dir.empty()) dir = "./";
    }
    return dir;
}

const std::string &dataDir()
{
    static std::string dir;
    if (dir.empty()) {
        const char *env = platformEnv("CROSSY_DATA");
        const std::string candidates[] = {env ? std::string(env) + "/" : std::string(), baseDir() + "data/",
                                          baseDir() + "../../data/", "data/"};
        for (const std::string &c : candidates) {
            if (c.empty()) continue;
            FILE *f = std::fopen((c + "manifest.txt").c_str(), "rb");
            if (f) {
                std::fclose(f);
                dir = c;
                break;
            }
        }
        if (dir.empty()) dir = baseDir() + "data/";
    }
    return dir;
}

bool loadManifest(const std::string &path, Manifest &out)
{
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return false;
    std::string text(buf.begin(), buf.end());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty() || line[0] == '#') continue;
        char kind[16] = {0}, a[128] = {0}, b[128] = {0}, c[128] = {0};
        int n = std::sscanf(line.c_str(), "%15s %127s %127s %127s", kind, a, b, c);
        if (n == 4 && !std::strcmp(kind, "model")) out.models[a] = {b, c};
        else if (n >= 2 && !std::strcmp(kind, "sound")) out.sounds.push_back(a);
        else if (n >= 2 && !std::strcmp(kind, "music")) out.music.push_back(a);
    }
    return !out.models.empty();
}

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? size_t(size) : 0);
    bool ok = size >= 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

namespace {

struct Reader {
    const std::vector<uint8_t> &b;
    size_t pos = 0;
    bool ok = true;
    explicit Reader(const std::vector<uint8_t> &buf) : b(buf) {}
    bool need(size_t n)
    {
        if (pos + n > b.size()) ok = false;
        return ok;
    }
    bool magic(const char *m)
    {
        if (!need(4) || std::memcmp(&b[pos], m, 4) != 0) return ok = false;
        pos += 4;
        return true;
    }
    uint32_t u32()
    {
        if (!need(4)) return 0;
        uint32_t v = uint32_t(b[pos]) | uint32_t(b[pos + 1]) << 8 | uint32_t(b[pos + 2]) << 16 | uint32_t(b[pos + 3]) << 24;
        pos += 4;
        return v;
    }
    uint16_t u16()
    {
        if (!need(2)) return 0;
        uint16_t v = uint16_t(b[pos] | b[pos + 1] << 8);
        pos += 2;
        return v;
    }
    uint8_t u8()
    {
        if (!need(1)) return 0;
        return b[pos++];
    }
    float f32()
    {
        uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
};

} // namespace

bool loadMesh(const std::string &path, MeshData &out)
{
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return false;
    Reader r(buf);
    if (!r.magic("CRM1")) return false;
    uint32_t vc = r.u32(), ic = r.u32();
    for (float &v : out.aabbMin) v = r.f32();
    for (float &v : out.aabbMax) v = r.f32();
    if (!r.need(size_t(vc) * 32 + size_t(ic) * 2)) return false;
    out.vertices.resize(size_t(vc) * 8);
    for (float &v : out.vertices) v = r.f32();
    out.indices.resize(ic);
    for (uint16_t &i : out.indices) i = r.u16();
    return r.ok;
}

bool loadFlatMesh(const std::string &path, FlatMeshData &out)
{
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return false;
    Reader r(buf);
    if (!r.magic("CRFM")) return false;
    uint32_t vc = r.u32(), tc = r.u32(), cc = r.u32();
    // the aabb is copied bit for bit (no float arithmetic: the SF2000 converts it with realFromFloat)
    for (float &v : out.aabbMin) v = r.f32();
    for (float &v : out.aabbMax) v = r.f32();
    if (vc > 65536 || cc > 256 || !r.need(size_t(cc) * 3 + size_t(vc) * 12 + size_t(tc) * 8)) return false;
    out.colors.resize(size_t(cc) * 3);
    for (uint8_t &c : out.colors) c = r.u8();
    out.positions.resize(size_t(vc) * 3);
    for (int32_t &p : out.positions) p = int32_t(r.u32());
    out.triangles.resize(tc);
    for (FlatMeshData::Triangle &t : out.triangles) {
        t.a = r.u16();
        t.b = r.u16();
        t.c = r.u16();
        t.color = r.u8();
        t.axis = r.u8();
        if (t.a >= vc || t.b >= vc || t.c >= vc || t.color >= cc || t.axis > 5) return false;
    }
    return r.ok;
}

bool loadTexture(const std::string &path, TextureData &out)
{
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return false;
    Reader r(buf);
    if (!r.magic("CRT1")) return false;
    out.width = r.u16();
    out.height = r.u16();
    out.channels = r.u8();
    r.u8();
    uint32_t runs = r.u32();
    size_t total = size_t(out.width) * size_t(out.height) * size_t(out.channels);
    out.pixels.resize(total);
    size_t at = 0;
    for (uint32_t i = 0; i < runs && r.ok; i++) {
        uint16_t n = r.u16();
        uint8_t px[4] = {0, 0, 0, 0};
        for (int c = 0; c < out.channels; c++) px[c] = r.u8();
        if (at + size_t(n) * out.channels > total) return false;
        for (uint16_t k = 0; k < n; k++, at += out.channels) std::memcpy(&out.pixels[at], px, size_t(out.channels));
    }
    return r.ok && at == total;
}

bool loadSound(const std::string &path, SoundData &out)
{
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return false;
    Reader r(buf);
    if (!r.magic("CRS1")) return false;
    out.sampleRate = int(r.u32());
    uint32_t frames = r.u32();
    if (!r.need(size_t(frames) * 2)) return false;
    out.samples.resize(frames);
    for (int16_t &s : out.samples) s = int16_t(r.u16());
    return r.ok;
}

bool loadFont(const std::string &path, FontData &out)
{
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return false;
    Reader r(buf);
    if (!r.magic("CRF1")) return false;
    out.atlasW = r.u16();
    out.atlasH = r.u16();
    out.pixelSize = r.u16();
    out.lineHeight = r.u16();
    uint16_t count = r.u16();
    for (uint16_t i = 0; i < count && r.ok; i++) {
        uint16_t cp = r.u16();
        Glyph g;
        g.x = r.u16();
        g.y = r.u16();
        g.w = r.u16();
        g.h = r.u16();
        g.xoff = int16_t(r.u16());
        g.yoff = int16_t(r.u16());
        g.advance = r.u16();
        const int slot = glyphSlot(cp);
        if (slot >= 0) out.glyphs[slot] = g;
    }
    size_t n = size_t(out.atlasW) * size_t(out.atlasH);
    if (!r.need(n)) return false;
    out.coverage.assign(buf.begin() + long(r.pos), buf.begin() + long(r.pos + n));
    return true;
}

} // namespace cr
