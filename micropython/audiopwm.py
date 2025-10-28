"""Audio playback over PWM for ESP32-C3 in MicroPython."""
from __future__ import annotations

import _thread
import time

from machine import PWM, Pin

try:  # pragma: no cover - MicroPython specific
    from micropython import const
except ImportError:  # pragma: no cover - CPython compatibility for linting
    def const(value):
        return value


_DEFAULT_CHUNK_SIZE = const(1024)


class AudioPWM:
    """Stream unsigned 8-bit PCM files over LEDC PWM."""

    def __init__(
        self,
        pin: int = 2,
        pwm_base_freq: int = 20_000,
        sample_rate: int = 8_000,
        chunk_size: int = _DEFAULT_CHUNK_SIZE,
    ) -> None:
        self._pin = Pin(pin, Pin.OUT)
        self._pwm = PWM(self._pin, freq=pwm_base_freq, duty_u16=0)
        self._sample_rate = sample_rate
        self._chunk_size = max(128, int(chunk_size))
        self._volume = 1.0
        self._file = None
        self._buffer = bytearray(self._chunk_size)
        self._buf_len = 0
        self._buf_pos = 0
        self._playing = False
        self._stop_requested = False
        self._thread_id = None
        self._period_us = max(1, int(1_000_000 / self._sample_rate))

    def set_volume(self, gain01: float) -> None:
        gain = 0.0 if gain01 < 0.0 else 1.0 if gain01 > 1.0 else gain01
        self._volume = gain

    def play_file(self, path: str) -> bool:
        self.stop()
        try:
            f = open(path, "rb")
        except OSError:
            return False

        self._file = f
        self._stop_requested = False
        self._playing = True
        self._buf_len = 0
        self._buf_pos = 0

        first = self._file.read(self._chunk_size)
        if not first:
            self.stop()
            return False
        self._buffer[: len(first)] = first
        self._buf_len = len(first)
        self._buf_pos = 0

        self._thread_id = _thread.start_new_thread(self._playback_loop, ())
        return True

    def is_playing(self) -> bool:
        return self._playing

    def stop(self) -> None:
        self._stop_requested = True
        self._playing = False
        if self._thread_id is not None:
            # Busy wait for the worker to exit.
            while self._thread_id is not None:
                time.sleep_ms(5)
        if self._file is not None:
            try:
                self._file.close()
            except OSError:
                pass
            self._file = None
        self._buf_pos = 0
        self._buf_len = 0
        self._pwm.duty_u16(0)

    # Internal methods -------------------------------------------------
    def _playback_loop(self) -> None:
        period = self._period_us
        next_tick = time.ticks_add(time.ticks_us(), period)
        try:
            while not self._stop_requested:
                if self._buf_pos >= self._buf_len:
                    if not self._refill():
                        break
                sample = self._buffer[self._buf_pos]
                self._buf_pos += 1
                sample = self._apply_volume(sample)
                self._pwm.duty_u16(sample << 8)

                while time.ticks_diff(next_tick, time.ticks_us()) > 0:
                    pass
                next_tick = time.ticks_add(next_tick, period)
        finally:
            self._playing = False
            self._stop_requested = False
            self._thread_id = None
            self._pwm.duty_u16(0)

    def _refill(self) -> bool:
        if self._file is None:
            return False
        data = self._file.read(self._chunk_size)
        if not data:
            return False
        data_len = len(data)
        self._buffer[:data_len] = data
        self._buf_len = data_len
        self._buf_pos = 0
        return True

    def _apply_volume(self, sample: int) -> int:
        centered = (sample - 128) * self._volume
        value = int(centered + 128)
        if value < 0:
            value = 0
        elif value > 255:
            value = 255
        return value
