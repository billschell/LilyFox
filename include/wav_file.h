#pragma once

#include <stddef.h>
#include <stdint.h>

#include <SD.h>

// Minimal RIFF/WAV reader for recordings on the SD card. Accepts mono
// PCM, 8- or 16-bit, 4-48 kHz; anything else is rejected with a serial
// log explaining what to re-export.
class WavFile
{
public:
    // Opens the file and parses the header up to the start of sample data.
    bool open(const char *path);
    void close();

    uint32_t sampleRate() const { return sample_rate_; }
    uint32_t durationMs() const;
    size_t sampleCount() const;

    // Reads up to maxSamples samples converted to signed 16-bit.
    // Returns the number read; 0 at end of data.
    size_t readSamples(int16_t *dest, size_t maxSamples);

    // Duration of a file without keeping it open (for schedule fitting).
    // Returns 0 if the file is missing or invalid.
    static uint32_t durationOfMs(const char *path);

private:
    bool _parseHeader(const char *path);

    File file_;
    uint32_t sample_rate_ = 0;
    uint32_t data_bytes_total_ = 0;
    uint32_t data_bytes_left_ = 0;
    uint8_t bits_per_sample_ = 0;
};
