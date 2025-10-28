#include "AudioPWM.h"

#include <LittleFS.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstring>
#include <vector>

#ifndef AUDIOPWM_CHUNK_SIZE
#define AUDIOPWM_CHUNK_SIZE 1024
#endif

#ifndef AUDIOPWM_RING_BUFFERS
#define AUDIOPWM_RING_BUFFERS 2
#endif

namespace AudioPWM {
namespace {

constexpr ledc_channel_t kPwmChannel = LEDC_CHANNEL_0;
constexpr uint32_t kSilenceLevel = 128;
constexpr TickType_t kReaderDelayTicks = 1;

struct PlayerState {
  bool initialized = false;
  int pin = 2;
  int pwmFreq = 20000;
  int pwmResolution = 8;
  uint32_t sampleRate = 8000;
  uint32_t samplePeriodUs = 125;
  float volume = 1.0f;
  File currentFile;
  std::vector<uint8_t> ringBuffer;
  size_t readIndex = 0;
  size_t writeIndex = 0;
  size_t buffered = 0;
  bool fileEnded = false;
  bool playing = false;
  bool timerRunning = false;
  TaskHandle_t readerTask = nullptr;
};

PlayerState state;
portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;
esp_timer_handle_t playbackTimer = nullptr;

inline uint32_t clampDutyFromSample(uint8_t sample) {
  const uint32_t maxDuty = (1u << state.pwmResolution) - 1u;
  uint32_t duty = (static_cast<uint32_t>(sample) * maxDuty + 127) / 255;
  return duty;
}

inline uint8_t applyVolume(uint8_t sample) {
  int centered = static_cast<int>(sample) - static_cast<int>(kSilenceLevel);
  centered = static_cast<int>(centered * state.volume);
  int adjusted = centered + static_cast<int>(kSilenceLevel);
  if (adjusted < 0) {
    adjusted = 0;
  } else if (adjusted > 255) {
    adjusted = 255;
  }
  return static_cast<uint8_t>(adjusted);
}

void closeCurrentFile() {
  if (state.currentFile) {
    state.currentFile.close();
  }
}

void stopPlaybackTimer() {
  if (playbackTimer != nullptr) {
    esp_timer_stop(playbackTimer);
  }
}

void playbackTimerCallback(void* /*arg*/) {
  uint8_t rawSample = static_cast<uint8_t>(kSilenceLevel);
  bool shouldStop = false;

  portENTER_CRITICAL(&bufferMux);
  if (state.buffered > 0) {
    rawSample = state.ringBuffer[state.readIndex];
    state.readIndex = (state.readIndex + 1) % state.ringBuffer.size();
    state.buffered--;
    if (state.buffered == 0 && state.fileEnded) {
      state.playing = false;
      shouldStop = true;
    }
  } else if (state.fileEnded) {
    state.playing = false;
    shouldStop = true;
  }
  portEXIT_CRITICAL(&bufferMux);

  uint8_t adjustedSample = applyVolume(rawSample);
  ledcWrite(kPwmChannel, clampDutyFromSample(adjustedSample));

  if (shouldStop) {
    stopPlaybackTimer();
    portENTER_CRITICAL(&bufferMux);
    state.timerRunning = false;
    portEXIT_CRITICAL(&bufferMux);
  }
}

void readerTask(void* /*param*/) {
  const size_t bufferSize = state.ringBuffer.size();
  std::vector<uint8_t> temp(AUDIOPWM_CHUNK_SIZE);

  while (true) {
    if (!state.playing) {
      break;
    }

    size_t toRead = 0;
    size_t writeIndex = 0;

    portENTER_CRITICAL(&bufferMux);
    if (!state.playing) {
      portEXIT_CRITICAL(&bufferMux);
      break;
    }

    if (state.buffered < bufferSize) {
      size_t space = bufferSize - state.buffered;
      size_t contiguous = bufferSize - state.writeIndex;
      toRead = std::min({space, contiguous, static_cast<size_t>(AUDIOPWM_CHUNK_SIZE)});
      writeIndex = state.writeIndex;
    }
    portEXIT_CRITICAL(&bufferMux);

    if (toRead == 0) {
      vTaskDelay(kReaderDelayTicks);
      continue;
    }

    size_t bytesRead = state.currentFile.read(temp.data(), toRead);
    if (bytesRead == 0) {
      portENTER_CRITICAL(&bufferMux);
      state.fileEnded = true;
      portEXIT_CRITICAL(&bufferMux);
      break;
    }

    memcpy(&state.ringBuffer[writeIndex], temp.data(), bytesRead);

    portENTER_CRITICAL(&bufferMux);
    state.writeIndex = (writeIndex + bytesRead) % bufferSize;
    state.buffered += bytesRead;
    portEXIT_CRITICAL(&bufferMux);
  }

  portENTER_CRITICAL(&bufferMux);
  state.readerTask = nullptr;
  portEXIT_CRITICAL(&bufferMux);

  vTaskDelete(nullptr);
}

bool ensureTimer() {
  if (playbackTimer != nullptr) {
    return true;
  }

  esp_timer_create_args_t args = {
      .callback = &playbackTimerCallback,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "audiopwm"};

  return esp_timer_create(&args, &playbackTimer) == ESP_OK;
}

bool startReaderTask() {
  if (state.readerTask != nullptr) {
    return true;
  }
  BaseType_t ok = xTaskCreate(readerTask, "audiopwm_reader", 3072, nullptr, 1, &state.readerTask);
  return ok == pdPASS;
}

}  // namespace

bool begin(int audioPin, int pwmBaseFreq, int pwmResBits) {
  if (pwmResBits <= 1 || pwmResBits > 15) {
    return false;
  }

  state.pin = audioPin;
  state.pwmFreq = pwmBaseFreq;
  state.pwmResolution = pwmResBits;
  state.ringBuffer.assign(AUDIOPWM_CHUNK_SIZE * AUDIOPWM_RING_BUFFERS, kSilenceLevel);

  ledcDetachPin(audioPin);
  if (!ledcSetup(kPwmChannel, pwmBaseFreq, pwmResBits)) {
    return false;
  }
  ledcAttachPin(audioPin, kPwmChannel);
  ledcWrite(kPwmChannel, clampDutyFromSample(kSilenceLevel));

  if (!ensureTimer()) {
    return false;
  }

  state.initialized = true;
  return true;
}

bool setSampleRate(uint32_t hz) {
  if (hz == 0) {
    return false;
  }

  uint32_t period = 1000000UL / hz;
  if (period == 0) {
    return false;
  }

  portENTER_CRITICAL(&bufferMux);
  state.sampleRate = hz;
  state.samplePeriodUs = period;
  portEXIT_CRITICAL(&bufferMux);

  if (state.timerRunning && playbackTimer != nullptr) {
    esp_timer_stop(playbackTimer);
    if (esp_timer_start_periodic(playbackTimer, state.samplePeriodUs) != ESP_OK) {
      portENTER_CRITICAL(&bufferMux);
      state.timerRunning = false;
      portEXIT_CRITICAL(&bufferMux);
      return false;
    }
  }

  return true;
}

void setVolume(float gain01) {
  if (gain01 < 0.0f) {
    gain01 = 0.0f;
  } else if (gain01 > 1.0f) {
    gain01 = 1.0f;
  }
  portENTER_CRITICAL(&bufferMux);
  state.volume = gain01;
  portEXIT_CRITICAL(&bufferMux);
}

bool playFile(const char* littlefsPath) {
  if (!state.initialized || littlefsPath == nullptr) {
    return false;
  }

  stop();

  File file = LittleFS.open(littlefsPath, "r");
  if (!file || file.isDirectory()) {
    return false;
  }

  state.currentFile = file;

  portENTER_CRITICAL(&bufferMux);
  state.readIndex = 0;
  state.writeIndex = 0;
  state.buffered = 0;
  state.fileEnded = false;
  state.playing = true;
  portEXIT_CRITICAL(&bufferMux);

  // Prime buffer with initial data.
  size_t bytesRead = state.currentFile.read(state.ringBuffer.data(), state.ringBuffer.size());
  if (bytesRead == 0) {
    stop();
    return false;
  }

  portENTER_CRITICAL(&bufferMux);
  state.writeIndex = bytesRead % state.ringBuffer.size();
  state.buffered = bytesRead;
  portEXIT_CRITICAL(&bufferMux);

  if (!startReaderTask()) {
    stop();
    return false;
  }

  if (esp_timer_start_periodic(playbackTimer, state.samplePeriodUs) != ESP_OK) {
    stop();
    return false;
  }

  portENTER_CRITICAL(&bufferMux);
  state.timerRunning = true;
  portEXIT_CRITICAL(&bufferMux);

  return true;
}

bool isPlaying() {
  bool playing = false;
  portENTER_CRITICAL(&bufferMux);
  playing = state.playing || (state.buffered > 0) || state.timerRunning;
  portEXIT_CRITICAL(&bufferMux);
  return playing;
}

void stop() {
  portENTER_CRITICAL(&bufferMux);
  state.playing = false;
  state.fileEnded = true;
  portEXIT_CRITICAL(&bufferMux);

  if (playbackTimer != nullptr) {
    esp_timer_stop(playbackTimer);
  }

  portENTER_CRITICAL(&bufferMux);
  state.timerRunning = false;
  portEXIT_CRITICAL(&bufferMux);

  // Wait for reader task to finish if running.
  TaskHandle_t readerHandle = nullptr;
  do {
    portENTER_CRITICAL(&bufferMux);
    readerHandle = state.readerTask;
    portEXIT_CRITICAL(&bufferMux);
    if (readerHandle != nullptr) {
      vTaskDelay(kReaderDelayTicks);
    }
  } while (readerHandle != nullptr);

  closeCurrentFile();

  portENTER_CRITICAL(&bufferMux);
  state.readIndex = 0;
  state.writeIndex = 0;
  state.buffered = 0;
  portEXIT_CRITICAL(&bufferMux);

  ledcWrite(kPwmChannel, clampDutyFromSample(kSilenceLevel));
}

}  // namespace AudioPWM
