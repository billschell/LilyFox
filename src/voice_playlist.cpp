#include "voice_playlist.h"

#include <SD.h>
#include <SPI.h>
#include <algorithm>

#include "board_pins.h"

bool VoicePlaylist::begin()
{
    SPI.begin(boardpins::SD_SPI_SCK, boardpins::SD_SPI_MISO, boardpins::SD_SPI_MOSI,
              boardpins::SD_CS);
    if (!SD.begin(boardpins::SD_CS, SPI))
    {
        Serial.println("No SD card detected");
        return false;
    }

    File root = SD.open("/");
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile())
    {
        const String name = entry.name();
        const bool is_wav = name.endsWith(".wav") || name.endsWith(".WAV");
        // Skip directories and hidden/metadata files (e.g. macOS "._x.wav").
        if (!entry.isDirectory() && is_wav && !name.startsWith(".") && !name.startsWith("_"))
            files_.push_back("/" + name);
        entry.close();
    }
    root.close();

    std::sort(files_.begin(), files_.end(),
              [](const String &left, const String &right) {
                  return strcasecmp(left.c_str(), right.c_str()) < 0;
              });

    Serial.printf("SD card: %u wav file(s)\n", files_.size());
    for (const String &file : files_)
        Serial.printf("  %s\n", file.c_str());
    return true;
}

size_t VoicePlaylist::count() const
{
    return files_.size();
}

const char *VoicePlaylist::peekNext() const
{
    if (files_.empty())
        return nullptr;
    return files_[next_index_].c_str();
}

void VoicePlaylist::advance()
{
    if (files_.empty())
        return;
    next_index_ = (next_index_ + 1) % files_.size();
}
