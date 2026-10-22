#include "streamvolume.h"

#include <atomic>

namespace LocalRdp {

static std::atomic<bool> s_On { true };
static std::atomic<int> s_Volume { 100 };

void setStreamAudio(bool on, int volumePercent)
{
    s_On = on;
    s_Volume = volumePercent < 0 ? 0 : volumePercent > 100 ? 100 : volumePercent;
}

bool streamAudioOn()
{
    return s_On;
}

int streamVolume()
{
    return s_Volume;
}

void applyStreamVolume(float* samples, int count, int volumePercent)
{
    if (volumePercent < 0 || volumePercent >= 100) {
        return;
    }
    const float gain = volumePercent / 100.0f;
    for (int i = 0; i < count; i++) {
        samples[i] *= gain;
    }
}

void applyStreamVolume(short* samples, int count, int volumePercent)
{
    if (volumePercent < 0 || volumePercent >= 100) {
        return;
    }
    for (int i = 0; i < count; i++) {
        samples[i] = (short)((int)samples[i] * volumePercent / 100);
    }
}

}
