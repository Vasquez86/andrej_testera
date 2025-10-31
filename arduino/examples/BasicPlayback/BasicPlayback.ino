#include <Arduino.h>
#include <LittleFS.h>

#include "AudioPWM.h"

void setup() {
  Serial.begin(115200);
  Serial.println("AudioPWM example");

  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount LittleFS");
    while (true) {
      delay(1000);
    }
  }

  if (!AudioPWM::begin(2, 20000, 8)) {
    Serial.println("AudioPWM begin failed");
    while (true) {
      delay(1000);
    }
  }

  AudioPWM::setSampleRate(8000);
  AudioPWM::setVolume(0.9f);

  if (!AudioPWM::playFile("/chainsaw.pcm")) {
    Serial.println("Unable to play /chainsaw.pcm");
  }
}

void loop() {
  if (!AudioPWM::isPlaying()) {
    delay(500);
    AudioPWM::playFile("/laugh.pcm");
  }
}
