# ESP32-C3 MicroPython PWM Audio Playback

MicroPython utilities for playing PCM RAW clips (8-bit unsigned or 16-bit signed) on the ESP32-C3 SuperMini. Audio is generated with the LEDC PWM
peripheral and a simple RC low-pass filter so it can be amplified and sent to a small speaker.

## Hardware quick reference

- Output pin: default **GPIO2** (configurable).
- ESP32-C3 has no DAC → use LEDC PWM + RC filter (1–4.7 kΩ in series, 100–220 nF to GND).
- Drive an external amplifier (LM386/TDA1308/transistor stage) before the speaker.
- Audio format: **Mono, header-less PCM**. Supports unsigned 8-bit or little-endian signed 16-bit samples. Default rate 8 kHz (11_025/16_000 Hz also work).

## Project layout

- [`micropython/audiopwm.py`](micropython/audiopwm.py): LEDC-based audio player.
- [`micropython/main.py`](micropython/main.py): example entrypoint that plays `/testera.raw` on boot (16-bit signed PCM example).
- [`testera.raw`](testera.raw): sample 16-bit signed PCM clip for quick testing.

## Using the player

```python
from audiopwm import AudioPWM
import time

player = AudioPWM(
    pin=2,
    pwm_base_freq=20_000,
    sample_rate=8_000,
    sample_bits=16,
    signed=True,
)
player.set_volume(0.9)

if not player.play_file('/chainsaw.pcm'):
    print('Failed to start playback')

while player.is_playing():
    time.sleep_ms(50)
```

Use `sample_bits` and `signed` to match your clip format: keep the defaults for 8-bit unsigned data or set `sample_bits=16` (and optionally `signed=True` for the default little-endian signed clips) when working with higher fidelity audio.

### Highlights

- Uses `machine.PWM` with a software-timed worker thread to achieve the target sample rate.
- Streams data in 1 KB chunks by default (tunable via `chunk_size`).
- Volume scaling and graceful shutdown, including cleanup when playback finishes.
- Handles unsigned 8-bit and signed 16-bit RAW clips (little-endian) with automatic PWM scaling.

### Deployment

1. Flash a recent ESP32-C3 MicroPython firmware.
2. Copy `audiopwm.py`, `main.py`, and your PCM files (`chainsaw.pcm`, `laugh.pcm`, …) to the device root using `mpremote`, Thonny, etc.
3. Adjust `PLAYER_PIN`, `PWM_BASE_FREQ`, `SAMPLE_RATE`, and `CLIP_PATH` in `main.py` if desired.
4. Reset the board or run `main.py` manually to begin playback.

## Preparing PCM RAW clips

In Audacity:

1. Mix the track down to **Mono**.
2. Set *Project Rate* to **8000 Hz** (or 11025/16000 Hz if desired).
3. Export as **Other uncompressed files** → *Header*: `RAW (header-less)`, *Encoding*: `Unsigned 8-bit PCM` or `Signed 16-bit PCM` (little-endian) depending on your chosen depth.
4. Name the files `chainsaw.pcm`, `laugh.pcm`, etc., and upload them to the device filesystem.

Enjoy PWM-powered sound on the ESP32-C3 SuperMini — now entirely in MicroPython!
