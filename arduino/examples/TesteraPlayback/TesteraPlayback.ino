#include <Arduino.h>
#include <LittleFS.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "AudioPWM.h"

namespace {
constexpr int AUDIO_PIN = 2;           // PWM output sent through RC filter → amplifier
constexpr int WAKE_PIN = 4;            // RTC-capable GPIO for wake-up (touch to 5 V via resistor divider)
constexpr uint32_t SAMPLE_RATE = 8000; // Hz, matches unsigned 8-bit PCM clip
constexpr char AUDIO_PATH[] = "/testera.raw";
bool fsMounted = false;

void logWakeReason() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("Cold boot");
      break;
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Woke from EXT0 (WAKE_PIN high)");
      break;
    default:
      Serial.printf("Wake cause: %d\n", static_cast<int>(cause));
      break;
  }
}

void enterDeepSleep() {
  Serial.println("Entering deep sleep. Touch WAKE_PIN to 5 V via resistor divider to wake.");
  Serial.flush();
  AudioPWM::stop();
  if (fsMounted) {
    LittleFS.end();
  }
  esp_deep_sleep_start();
  while (true) {
    delay(1000);
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("Testera RAW auto playback");

  pinMode(WAKE_PIN, INPUT_PULLDOWN);
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(WAKE_PIN), 1);
  logWakeReason();

  fsMounted = LittleFS.begin(false);
  if (!fsMounted) {
    Serial.println("LittleFS mount failed. Ensure /testera.raw is uploaded (LittleFS data folder).");
    enterDeepSleep();
  }

  if (!AudioPWM::begin(AUDIO_PIN, 20000, 8)) {
    Serial.println("AudioPWM initialisation failed");
    enterDeepSleep();
  }

  AudioPWM::setSampleRate(SAMPLE_RATE);
  AudioPWM::setVolume(1.0f);

  Serial.printf("Playing %s...\n", AUDIO_PATH);
  if (!AudioPWM::playFile(AUDIO_PATH)) {
    Serial.println("Playback start failed");
    enterDeepSleep();
  }

  while (AudioPWM::isPlaying()) {
    delay(10);
  }

  Serial.println("Playback finished");
  delay(50);
  enterDeepSleep();
}

void loop() {
  // Not used – the board spends its time in deep sleep.
}
