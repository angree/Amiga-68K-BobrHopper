// Microsoft ADPCM (WAV format 2), mono: integer decoding and a looping reader streamed from a file, for the SF2000
// music (task C2.3; the approach of the user's SF2000 game Santa, which streams its music this way). Built by
// tools/bake_music_adpcm.py; decoding matches ffmpeg's adpcm_ms decoder sample for sample (tests/test_sw_audio.cpp).
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace cr {

struct AdpcmWav {
    FILE *file = nullptr;
    // or the whole file in memory (adpcmOpenMemory): no card access while the track plays, loops or changes
    std::shared_ptr<const std::vector<uint8_t>> memory;
    size_t memoryPos = 0;
    int sampleRate = 0, channels = 0, blockAlign = 0, samplesPerBlock = 0;
    uint32_t dataStart = 0, dataSize = 0, bytesRead = 0;
    long totalSamples = 0; // what one pass through the data decodes to
    std::vector<uint8_t> block;
    std::vector<int16_t> decoded;
    int decodedCount = 0, decodedPos = 0;
};

// parses the RIFF header (mono MS ADPCM only) and leaves the file at the first block
bool adpcmOpen(const std::string &path, AdpcmWav &wav);
// the same from a file already read into memory (shared: the track may be opened again later)
bool adpcmOpenMemory(std::shared_ptr<const std::vector<uint8_t>> data, AdpcmWav &wav);
bool adpcmIsOpen(const AdpcmWav &wav);
void adpcmClose(AdpcmWav &wav);
// the next `frames` samples, starting over at the end of the data; returns how many came from the file (the rest of
// `out` is silence, only when the file cannot be read)
int adpcmRead(AdpcmWav &wav, int16_t *out, int frames);
// one block: header (predictor, delta, sample1, sample2) and nibbles, high nibble first; returns the samples written
int adpcmDecodeBlockMono(const uint8_t *src, int size, int16_t *dst, int maxSamples);

} // namespace cr
