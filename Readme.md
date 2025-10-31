# ESP32-C3 PWM Audio Playback

Two minimal runtimes for playing unsigned 8-bit PCM RAW clips stored on the ESP32-C3 SuperMini flash. Audio is generated with the LEDC PWM peripheral and a simple RC low-pass filter so it can be amplified and sent to a small speaker.

## Hardware quick reference

- Output pin: default **GPIO2** (configurable).
- ESP32-C3 has no DAC → use LEDC PWM + RC filter (1–4.7 kΩ in series, 100–220 nF to GND).
- Drive an external amplifier (LM386/TDA1308/transistor stage) before the speaker.
- Audio format: **Mono, unsigned 8-bit PCM, RAW header-less**. Default rate 8 kHz (11_025/16_000 Hz also work).

## Arduino (C++ / LittleFS)

Files are located under [`arduino/`](arduino/):

- [`AudioPWM.h`](arduino/AudioPWM.h) / [`AudioPWM.cpp`](arduino/AudioPWM.cpp): LEDC based player implementation.
- [`examples/BasicPlayback/BasicPlayback.ino`](arduino/examples/BasicPlayback/BasicPlayback.ino): minimal usage example.

### API

```cpp
namespace AudioPWM {
  bool begin(int audioPin = 2, int pwmBaseFreq = 20000, int pwmResBits = 8);
  bool setSampleRate(uint32_t hz);
  void setVolume(float gain01);
  bool playFile(const char* littlefsPath);
  bool isPlaying();
  void stop();
}
```

### Features

- Uses LEDC PWM (quasi-DAC) driven by an `esp_timer` interrupt for precise sample timing.
- Streams from LittleFS using a ring buffer with a background reader task.
- Volume scaling (0.0–1.0), graceful stop, and non-blocking status checks.
- Configurable compile-time buffer size via `AUDIOPWM_CHUNK_SIZE` and `AUDIOPWM_RING_BUFFERS` macros.

### Getting started

1. Install **Arduino Core for ESP32** v2.0+ with ESP32-C3 support.
2. Add the LittleFS data upload plugin and place your RAW files in the data folder (e.g., `/chainsaw.pcm`, `/laugh.pcm`).
3. Upload the filesystem and the sketch from `examples/BasicPlayback`.
4. Optionally adjust `AUDIO_PIN`, `PWM_BASE_FREQ`, `SAMPLE_RATE_HZ`, and `PWM_RES_BITS` when calling `AudioPWM::begin` / `setSampleRate`.

## MicroPython

The MicroPython player lives in [`micropython/audiopwm.py`](micropython/audiopwm.py).

```python
from audiopwm import AudioPWM
import time

player = AudioPWM(pin=2, pwm_base_freq=20_000, sample_rate=8_000)
player.set_volume(0.9)

if not player.play_file('/chainsaw.pcm'):
    print('Failed to start playback')

while player.is_playing():
    time.sleep_ms(50)
```

### Highlights

- Uses `machine.PWM` with a software-timed worker thread to achieve the target sample rate.
- Streams data in 1 KB chunks by default (tunable via `chunk_size`).
- Volume scaling and graceful shutdown, including cleanup when playback finishes.

### Deployment

1. Flash a recent ESP32-C3 MicroPython firmware.
2. Copy `audiopwm.py` and your PCM files (`chainsaw.pcm`, `laugh.pcm`, …) to the device root using `mpremote`, Thonny, etc.
3. Run the example snippet (e.g., inside `main.py`).

## Preparing PCM RAW clips

In Audacity:

1. Mix the track down to **Mono**.
2. Set *Project Rate* to **8000 Hz** (or 11025/16000 Hz if desired).
3. Export as **Other uncompressed files** → *Header*: `RAW (header-less)`, *Encoding*: `Unsigned 8-bit PCM`.
4. Name the files `chainsaw.pcm`, `laugh.pcm`, etc., and upload them to the device filesystem.

Enjoy PWM-powered sound on the ESP32-C3 SuperMini!
