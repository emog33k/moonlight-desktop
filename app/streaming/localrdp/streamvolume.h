#pragma once

// The PC's sound as heard on the laptop: on/off and volume, applied to the
// stream's audio on the laptop only (the PC itself keeps its sound).
// Set from the stream window's thread, read by the audio thread.

namespace LocalRdp {

void setStreamAudio(bool on, int volumePercent); // volume clamped to 0..100
bool streamAudioOn();
int streamVolume();

// Scales decoded samples in place (100 = unchanged, out of range = 100).
void applyStreamVolume(float* samples, int count, int volumePercent);
void applyStreamVolume(short* samples, int count, int volumePercent);

}
