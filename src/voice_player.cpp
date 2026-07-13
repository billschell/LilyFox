#include "voice_player.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "fox_config.h"
#include "wav_file.h"

VoicePlayer::VoicePlayer(AudioOutput &audio) : audio_{audio}
{
}

bool VoicePlayer::load(const char *path)
{
    unload();

    WavFile wav;
    if (!wav.open(path))
        return false;

    const size_t total_samples = wav.sampleCount();
    if (total_samples == 0)
    {
        wav.close();
        return false;
    }

    samples_ = static_cast<int16_t *>(heap_caps_malloc(
        total_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (samples_ == nullptr)
    {
        Serial.printf("Not enough PSRAM for %s (%u samples)\n", path, total_samples);
        wav.close();
        return false;
    }

    size_t filled = 0;
    while (filled < total_samples)
    {
        const size_t want = total_samples - filled < 2048 ? total_samples - filled : 2048;
        const size_t got = wav.readSamples(samples_ + filled, want);
        if (got == 0)
            break;
        filled += got;
    }
    sample_rate_ = wav.sampleRate();
    wav.close();

    if (filled == 0)
    {
        unload();
        return false;
    }
    sample_count_ = filled;
    if (foxconfig::VOICE_LOWPASS_HZ > 0)
        _lowPass();
    if (foxconfig::VOICE_NORMALIZE)
        _normalizeLoudness();
    if (foxconfig::VOICE_TRIM_PAUSES)
        _trimPauses();
    Serial.printf("Loaded %s into PSRAM: %.1f s at %lu Hz\n", path,
                  durationMs() / 1000.0, static_cast<unsigned long>(sample_rate_));
    return true;
}

void VoicePlayer::_trimPauses()
{
    // A sample is "quiet" when a short sliding peak stays under the
    // gate; the first VOICE_MAX_PAUSE_MS of each quiet run is kept and
    // the remainder dropped. Speech resumes from near-silence, so the
    // splice is click-free.
    constexpr float PAUSE_GATE = 0.02f; // -34 dBFS after normalization
    const int16_t gate = static_cast<int16_t>(PAUSE_GATE * 32768.0f);
    const size_t max_pause =
        static_cast<size_t>(foxconfig::VOICE_MAX_PAUSE_MS) * sample_rate_ / 1000;
    // Hangover keeps brief inter-syllable dips from counting as pauses.
    const size_t hangover = sample_rate_ / 20; // 50 ms

    size_t write_index = 0;
    size_t quiet_run = 0;
    size_t since_loud = 0;
    for (size_t read_index = 0; read_index < sample_count_; read_index++)
    {
        const int16_t value = samples_[read_index];
        if (value > gate || value < -gate)
            since_loud = 0;
        else
            since_loud++;

        if (since_loud > hangover)
            quiet_run++;
        else
            quiet_run = 0;

        if (quiet_run > max_pause)
            continue; // drop the excess of a long pause
        samples_[write_index++] = value;
    }

    if (write_index < sample_count_)
    {
        const float trimmed_s =
            static_cast<float>(sample_count_ - write_index) / sample_rate_;
        Serial.printf("Trimmed %.1f s of pauses\n", trimmed_s);
        sample_count_ = write_index;
    }
}

void VoicePlayer::_lowPass()
{
    if (foxconfig::VOICE_LOWPASS_HZ * 2 >= sample_rate_)
        return; // nothing to remove below Nyquist

    // Second-order Butterworth low-pass (RBJ biquad), applied in place.
    const float omega = 2.0f * static_cast<float>(M_PI) *
                        foxconfig::VOICE_LOWPASS_HZ / sample_rate_;
    const float alpha = sinf(omega) / (2.0f * 0.7071f);
    const float cos_omega = cosf(omega);
    const float a0 = 1.0f + alpha;
    const float b0 = (1.0f - cos_omega) / 2.0f / a0;
    const float b1 = (1.0f - cos_omega) / a0;
    const float b2 = b0;
    const float a1 = -2.0f * cos_omega / a0;
    const float a2 = (1.0f - alpha) / a0;

    float in1 = 0, in2 = 0, out1 = 0, out2 = 0;
    for (size_t index = 0; index < sample_count_; index++)
    {
        const float input = samples_[index];
        const float output = b0 * input + b1 * in1 + b2 * in2 - a1 * out1 - a2 * out2;
        in2 = in1;
        in1 = input;
        out2 = out1;
        out1 = output;
        float clamped = output;
        if (clamped > 32767.0f)
            clamped = 32767.0f;
        else if (clamped < -32768.0f)
            clamped = -32768.0f;
        samples_[index] = static_cast<int16_t>(lrintf(clamped));
    }
}

void VoicePlayer::_normalizeLoudness()
{
    // RMS of the speech itself: gate out inter-word silence so quiet
    // recordings with long pauses are not over-boosted.
    constexpr float SILENCE_GATE = 0.01f; // -40 dBFS
    double sum_squares = 0.0;
    size_t counted = 0;
    for (size_t index = 0; index < sample_count_; index++)
    {
        const float value = samples_[index] / 32768.0f;
        if (fabsf(value) > SILENCE_GATE)
        {
            sum_squares += static_cast<double>(value) * value;
            counted++;
        }
    }
    if (counted < sample_count_ / 100)
        return; // essentially a silent file; leave it alone

    const float speech_rms = sqrtf(static_cast<float>(sum_squares / counted));
    float gain = foxconfig::VOICE_TARGET_RMS / speech_rms;
    if (gain > 16.0f)
        gain = 16.0f;
    else if (gain < 0.25f)
        gain = 0.25f;

    for (size_t index = 0; index < sample_count_; index++)
    {
        float value = samples_[index] * gain / 32768.0f;
        // Cubic soft limiter: linear at low level, smooth knee, hits
        // full scale at |v| = 1.5.
        if (value > 1.5f)
            value = 1.5f;
        else if (value < -1.5f)
            value = -1.5f;
        value = value * (1.0f - value * value / 6.75f);
        // 0.90: peak headroom below the morse tone's deviation so
        // pre-emphasized speech transients stay inside the channel.
        samples_[index] = static_cast<int16_t>(lrintf(value * 32767.0f * 0.90f));
    }
    Serial.printf("Normalized: %+.1f dB (speech RMS %.1f -> %.1f dBFS)\n",
                  20.0 * log10(gain), 20.0 * log10(speech_rms),
                  20.0 * log10(speech_rms * gain > 1.0f ? 1.0f : speech_rms * gain));
}

void VoicePlayer::unload()
{
    if (samples_ != nullptr)
    {
        heap_caps_free(samples_);
        samples_ = nullptr;
    }
    sample_count_ = 0;
    sample_rate_ = 0;
}

bool VoicePlayer::loaded() const
{
    return samples_ != nullptr;
}

uint32_t VoicePlayer::durationMs() const
{
    if (sample_rate_ == 0)
        return 0;
    return static_cast<uint32_t>(
        static_cast<uint64_t>(sample_count_) * 1000ULL / sample_rate_);
}

bool VoicePlayer::play(const AbortPredicate &abort)
{
    if (samples_ == nullptr)
        return false;

    const int32_t pilot_level =
        foxconfig::VOICE_PILOT_ENABLED ? foxconfig::VOICE_PILOT_LEVEL : 0;
    audio_.startVoice(sample_rate_, foxconfig::VOICE_GAIN,
                      foxconfig::VOICE_PILOT_HZ, pilot_level);

    // Feed the ring directly from PSRAM in small continuous reads.
    // (A batched-burst variant was tried and audibly clicked once per
    // second: concentrated PSRAM traffic couples into the mic line,
    // while this steady trickle stays below the transmit noise floor.)
    size_t queued_total = 0;
    bool aborted = false;
    while (!aborted && queued_total < sample_count_)
    {
        if (abort && abort())
        {
            aborted = true;
            break;
        }
        const size_t accepted =
            audio_.queueVoice(samples_ + queued_total, sample_count_ - queued_total);
        queued_total += accepted;
        if (accepted == 0)
            delay(5); // ring full: comfortably ahead of the ISR
    }

    while (!aborted && !audio_.voiceDrained())
    {
        if (abort && abort())
            aborted = true;
        delay(5);
    }

    if (aborted)
        audio_.abortVoice();
    else
        audio_.endVoice();
    return !aborted;
}
