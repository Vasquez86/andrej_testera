#ifndef AUDIO_PWM_H
#define AUDIO_PWM_H

#include <Arduino.h>

namespace AudioPWM {

bool begin(int audioPin = 2, int pwmBaseFreq = 20000, int pwmResBits = 8);
bool setSampleRate(uint32_t hz);
void setVolume(float gain01);
bool playFile(const char *littlefsPath);
bool isPlaying();
void stop();

}  // namespace AudioPWM

#endif  // AUDIO_PWM_H
