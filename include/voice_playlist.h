#pragma once

#include <stddef.h>

#include <WString.h>
#include <vector>

// Scans the SD card root for .wav files and hands them out in
// alphabetical rotation, so each transmission plays the next recording.
class VoicePlaylist
{
public:
    // Brings up SPI and the SD card, then scans for recordings.
    // Returns false if no card is present.
    bool begin();

    size_t count() const;

    // Path of the file the next transmission will play (nullptr when
    // the playlist is empty). advance() moves the rotation along.
    const char *peekNext() const;
    void advance();

private:
    std::vector<String> files_;
    size_t next_index_ = 0;
};
