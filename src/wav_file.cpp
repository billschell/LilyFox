#include "wav_file.h"

#include <Arduino.h>

namespace
{

uint16_t readU16(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readU32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

} // namespace

bool WavFile::open(const char *path)
{
    file_ = SD.open(path, FILE_READ);
    if (!file_)
    {
        Serial.printf("WAV: cannot open %s\n", path);
        return false;
    }
    if (!_parseHeader(path))
    {
        file_.close();
        return false;
    }
    return true;
}

bool WavFile::_parseHeader(const char *path)
{
    uint8_t riff[12];
    if (file_.read(riff, sizeof(riff)) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
    {
        Serial.printf("WAV: %s is not a RIFF/WAVE file\n", path);
        return false;
    }

    bool format_seen = false;
    while (true)
    {
        uint8_t chunk_header[8];
        if (file_.read(chunk_header, sizeof(chunk_header)) != sizeof(chunk_header))
        {
            Serial.printf("WAV: %s has no data chunk\n", path);
            return false;
        }
        const uint32_t chunk_size = readU32(chunk_header + 4);

        if (memcmp(chunk_header, "fmt ", 4) == 0)
        {
            uint8_t format[16];
            if (chunk_size < sizeof(format) ||
                file_.read(format, sizeof(format)) != sizeof(format))
            {
                Serial.printf("WAV: %s has a malformed fmt chunk\n", path);
                return false;
            }
            const uint16_t audio_format = readU16(format + 0);
            const uint16_t channels = readU16(format + 2);
            sample_rate_ = readU32(format + 4);
            bits_per_sample_ = static_cast<uint8_t>(readU16(format + 14));

            if (audio_format != 1)
            {
                Serial.printf("WAV: %s is compressed; re-export as PCM\n", path);
                return false;
            }
            if (channels != 1)
            {
                Serial.printf("WAV: %s has %u channels; re-export as mono\n", path, channels);
                return false;
            }
            if (bits_per_sample_ != 8 && bits_per_sample_ != 16)
            {
                Serial.printf("WAV: %s is %u-bit; re-export as 8- or 16-bit\n",
                              path, bits_per_sample_);
                return false;
            }
            if (sample_rate_ < 4000 || sample_rate_ > 48000)
            {
                Serial.printf("WAV: %s sample rate %lu out of range 4k-48k\n",
                              path, static_cast<unsigned long>(sample_rate_));
                return false;
            }
            format_seen = true;
            // Skip any fmt extension bytes (plus RIFF odd-size padding).
            const uint32_t extra = chunk_size - sizeof(format) + (chunk_size & 1);
            if (extra > 0)
                file_.seek(file_.position() + extra);
        }
        else if (memcmp(chunk_header, "data", 4) == 0)
        {
            if (!format_seen)
            {
                Serial.printf("WAV: %s has data before fmt\n", path);
                return false;
            }
            data_bytes_total_ = chunk_size;
            data_bytes_left_ = chunk_size;
            return true; // file is positioned at the first sample
        }
        else
        {
            file_.seek(file_.position() + chunk_size + (chunk_size & 1));
        }
    }
}

void WavFile::close()
{
    file_.close();
}

uint32_t WavFile::durationMs() const
{
    if (sample_rate_ == 0)
        return 0;
    return static_cast<uint32_t>(
        static_cast<uint64_t>(sampleCount()) * 1000ULL / sample_rate_);
}

size_t WavFile::sampleCount() const
{
    if (bits_per_sample_ == 0)
        return 0;
    return data_bytes_total_ / (bits_per_sample_ / 8);
}

size_t WavFile::readSamples(int16_t *dest, size_t maxSamples)
{
    if (data_bytes_left_ == 0)
        return 0;

    if (bits_per_sample_ == 16)
    {
        uint32_t want_bytes = maxSamples * 2;
        if (want_bytes > data_bytes_left_)
            want_bytes = data_bytes_left_ & ~1UL;
        // ESP32 is little-endian, same as WAV: read samples in place.
        const int got_bytes = file_.read(reinterpret_cast<uint8_t *>(dest), want_bytes);
        if (got_bytes <= 0)
        {
            data_bytes_left_ = 0;
            return 0;
        }
        data_bytes_left_ -= got_bytes;
        return static_cast<size_t>(got_bytes) / 2;
    }

    uint8_t raw[128];
    uint32_t want_bytes = maxSamples < sizeof(raw) ? maxSamples : sizeof(raw);
    if (want_bytes > data_bytes_left_)
        want_bytes = data_bytes_left_;
    const int got_bytes = file_.read(raw, want_bytes);
    if (got_bytes <= 0)
    {
        data_bytes_left_ = 0;
        return 0;
    }
    data_bytes_left_ -= got_bytes;
    for (int index = 0; index < got_bytes; index++)
        dest[index] = static_cast<int16_t>((static_cast<int16_t>(raw[index]) - 128) << 8);
    return static_cast<size_t>(got_bytes);
}

uint32_t WavFile::durationOfMs(const char *path)
{
    WavFile probe;
    if (!probe.open(path))
        return 0;
    const uint32_t duration = probe.durationMs();
    probe.close();
    return duration;
}
