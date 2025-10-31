# ESP32-C3 MicroPython PWM Audio Playback

MicroPython utilities for playing unsigned 8-bit PCM RAW clips on the ESP32-C3 SuperMini. Audio is generated with the LEDC PWM
peripheral and a simple RC low-pass filter so it can be amplified and sent to a small speaker.

## Hardware quick reference

- Output pin: default **GPIO2** (configurable).
- ESP32-C3 has no DAC → use LEDC PWM + RC filter (1–4.7 kΩ in series, 100–220 nF to GND).
- Drive an external amplifier (LM386/TDA1308/transistor stage) before the speaker.
- Audio format: **Mono, unsigned 8-bit PCM, RAW header-less**. Default rate 8 kHz (11_025/16_000 Hz also work).

## Project layout

- [`micropython/audiopwm.py`](micropython/audiopwm.py): LEDC-based audio player.
- [`micropython/main.py`](micropython/main.py): example entrypoint that plays `/testera.raw` on boot.
- [`testera.raw`](testera.raw): sample unsigned 8-bit PCM clip for quick testing.

## Using the player

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
2. Copy `audiopwm.py`, `main.py`, and your PCM files (`chainsaw.pcm`, `laugh.pcm`, …) to the device root using `mpremote`, Thonny, etc.
3. Adjust `PLAYER_PIN`, `PWM_BASE_FREQ`, `SAMPLE_RATE`, and `CLIP_PATH` in `main.py` if desired.
4. Reset the board or run `main.py` manually to begin playback.

## Preparing PCM RAW clips

In Audacity:

1. Mix the track down to **Mono**.
2. Set *Project Rate* to **8000 Hz** (or 11025/16000 Hz if desired).
3. Export as **Other uncompressed files** → *Header*: `RAW (header-less)`, *Encoding*: `Unsigned 8-bit PCM`.
4. Name the files `chainsaw.pcm`, `laugh.pcm`, etc., and upload them to the device filesystem.

Enjoy PWM-powered sound on the ESP32-C3 SuperMini — now entirely in MicroPython!
