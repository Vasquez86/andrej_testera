"""Example MicroPython entrypoint for AudioPWM playback on ESP32-C3."""
from __future__ import annotations

import time

from audiopwm import AudioPWM


PLAYER_PIN = 2
PWM_BASE_FREQ = 20_000
SAMPLE_RATE = 8_000
SAMPLE_BITS = 16
SIGNED_SAMPLES = True
CLIP_PATH = "/testera.raw"


def main() -> None:
    player = AudioPWM(
        pin=PLAYER_PIN,
        pwm_base_freq=PWM_BASE_FREQ,
        sample_rate=SAMPLE_RATE,
        sample_bits=SAMPLE_BITS,
        signed=SIGNED_SAMPLES,
    )
    player.set_volume(0.9)

    if not player.play_file(CLIP_PATH):
        print("Failed to start playback from", CLIP_PATH)
        return

    try:
        while player.is_playing():
            time.sleep_ms(50)
    finally:
        player.stop()


if __name__ == "__main__":
    main()
