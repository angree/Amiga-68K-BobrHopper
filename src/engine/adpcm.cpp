#include "adpcm.h"

#include <cstring>

namespace cr {

namespace {

const int kCoef1[7] = {256, 512, 0, 192, 240, 460, 392};
const int kCoef2[7] = {0, -256, 0, 64, 0, -208, -232};
const int kAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};

uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// the source of the bytes: the file, or the whole file in memory (adpcmOpenMemory)
size_t readBytes(AdpcmWav &w, uint8_t *dst, size_t n)
{
    if (w.memory) {
        const size_t size = w.memory->size();
        const size_t left = w.memoryPos < size ? size - w.memoryPos : 0;
        const size_t take = n < left ? n : left;
        if (take) std::memcpy(dst, w.memory->data() + w.memoryPos, take);
        w.memoryPos += take;
        return take;
    }
    return w.file ? std::fread(dst, 1, n, w.file) : 0;
}

void skipBytes(AdpcmWav &w, uint32_t n)
{
    if (w.memory) w.memoryPos += n;
    else std::fseek(w.file, long(n), SEEK_CUR);
}

void seekTo(AdpcmWav &w, uint32_t pos)
{
    if (w.memory) w.memoryPos = pos;
    else std::fseek(w.file, long(pos), SEEK_SET);
}

uint32_t tellPos(const AdpcmWav &w) { return w.memory ? uint32_t(w.memoryPos) : uint32_t(std::ftell(w.file)); }

bool nextBlock(AdpcmWav &w)
{
    const uint32_t remaining = w.dataSize - w.bytesRead;
    if (remaining < 7) return false;
    const size_t size = remaining < uint32_t(w.blockAlign) ? remaining : size_t(w.blockAlign);
    if (readBytes(w, w.block.data(), size) != size) return false;
    w.bytesRead += uint32_t(size);
    w.decodedCount = adpcmDecodeBlockMono(w.block.data(), int(size), w.decoded.data(), w.samplesPerBlock);
    w.decodedPos = 0;
    return w.decodedCount > 0;
}

// the RIFF header (mono MS ADPCM only); leaves the source at the first block
bool parseHeader(AdpcmWav &w)
{
    uint8_t head[12];
    bool ok = readBytes(w, head, 12) == 12 && !std::memcmp(head, "RIFF", 4) && !std::memcmp(head + 8, "WAVE", 4);
    int format = 0;
    while (ok) {
        uint8_t chunk[8];
        if (readBytes(w, chunk, 8) != 8) {
            ok = false;
            break;
        }
        const uint32_t size = le32(chunk + 4);
        if (!std::memcmp(chunk, "fmt ", 4)) {
            uint8_t fmt[32] = {0};
            const size_t take = size < sizeof fmt ? size : sizeof fmt;
            if (readBytes(w, fmt, take) != take) {
                ok = false;
                break;
            }
            format = le16(fmt);
            w.channels = le16(fmt + 2);
            w.sampleRate = int(le32(fmt + 4));
            w.blockAlign = le16(fmt + 12);
            w.samplesPerBlock = take >= 20 ? le16(fmt + 18) : 0;
            if (size > take) skipBytes(w, uint32_t(size - take));
        } else if (!std::memcmp(chunk, "data", 4)) {
            w.dataStart = tellPos(w);
            w.dataSize = size;
            break;
        } else {
            skipBytes(w, size + (size & 1));
        }
    }
    if (w.samplesPerBlock <= 0 && w.blockAlign > 7) w.samplesPerBlock = (w.blockAlign - 7) * 2 + 2;
    ok = ok && format == 2 && w.channels == 1 && w.blockAlign > 7 && w.dataSize > 0 && w.samplesPerBlock >= 2;
    if (!ok) return false;
    w.block.resize(size_t(w.blockAlign));
    w.decoded.resize(size_t(w.samplesPerBlock));
    const uint32_t full = w.dataSize / uint32_t(w.blockAlign), rest = w.dataSize % uint32_t(w.blockAlign);
    w.totalSamples = long(full) * w.samplesPerBlock + (rest >= 7 ? long((rest - 7) * 2 + 2) : 0);
    w.bytesRead = 0;
    w.decodedCount = w.decodedPos = 0;
    return true;
}

} // namespace

int adpcmDecodeBlockMono(const uint8_t *src, int size, int16_t *dst, int maxSamples)
{
    if (size < 7 || maxSamples < 2) return 0;
    const int predictor = src[0] > 6 ? 0 : src[0];
    int delta = int16_t(le16(src + 1));
    int s1 = int16_t(le16(src + 3)), s2 = int16_t(le16(src + 5));
    int n = 0;
    dst[n++] = int16_t(s2);
    dst[n++] = int16_t(s1);
    for (int i = 7; i < size && n < maxSamples; i++) {
        for (int shift = 4; shift >= 0 && n < maxSamples; shift -= 4) {
            const int nibble = (src[i] >> shift) & 0xf;
            // C division like the reference decoder (a shift would round negative predictions the other way)
            int p = (s1 * kCoef1[predictor] + s2 * kCoef2[predictor]) / 256;
            p += (nibble & 8 ? nibble - 16 : nibble) * delta;
            p = p < -32768 ? -32768 : p > 32767 ? 32767 : p;
            s2 = s1;
            s1 = p;
            delta = (kAdapt[nibble] * delta) >> 8;
            if (delta < 16) delta = 16;
            dst[n++] = int16_t(p);
        }
    }
    return n;
}

bool adpcmOpen(const std::string &path, AdpcmWav &w)
{
    adpcmClose(w);
    w.file = std::fopen(path.c_str(), "rb");
    if (!w.file) return false;
    if (!parseHeader(w)) {
        adpcmClose(w);
        return false;
    }
    return true;
}

bool adpcmOpenMemory(std::shared_ptr<const std::vector<uint8_t>> data, AdpcmWav &w)
{
    adpcmClose(w);
    if (!data || data->empty()) return false;
    w.memory = std::move(data);
    w.memoryPos = 0;
    if (!parseHeader(w)) {
        adpcmClose(w);
        return false;
    }
    return true;
}

bool adpcmIsOpen(const AdpcmWav &w) { return w.file != nullptr || w.memory != nullptr; }

void adpcmClose(AdpcmWav &w)
{
    if (w.file) std::fclose(w.file);
    w.file = nullptr;
    w.memory.reset();
    w.memoryPos = 0;
    w.bytesRead = 0;
    w.decodedCount = w.decodedPos = 0;
}

int adpcmRead(AdpcmWav &w, int16_t *out, int frames)
{
    int filled = 0, restarts = 0;
    while (adpcmIsOpen(w) && filled < frames && restarts < 2) {
        if (w.decodedPos >= w.decodedCount && !nextBlock(w)) {
            seekTo(w, w.dataStart); // the end of the track: loop
            w.bytesRead = 0;
            restarts++;
            continue;
        }
        restarts = 0;
        int n = w.decodedCount - w.decodedPos;
        if (n > frames - filled) n = frames - filled;
        std::memcpy(out + filled, w.decoded.data() + w.decodedPos, size_t(n) * sizeof(int16_t));
        w.decodedPos += n;
        filled += n;
    }
    if (filled < frames) std::memset(out + filled, 0, size_t(frames - filled) * sizeof(int16_t));
    return filled;
}

} // namespace cr
