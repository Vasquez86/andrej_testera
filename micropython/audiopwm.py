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
    """Stream PCM files (8-bit unsigned or 16-bit signed) over LEDC PWM."""

    def __init__(
        self,
        pin: int = 2,
        pwm_base_freq: int = 20_000,
        sample_rate: int = 8_000,
        chunk_size: int = _DEFAULT_CHUNK_SIZE,
        sample_bits: int = 8,
        signed: bool | None = None,
        little_endian: bool = True,
    ) -> None:
        self._pin = Pin(pin, Pin.OUT)
        self._pwm = PWM(self._pin, freq=pwm_base_freq, duty_u16=0)
        self._sample_rate = sample_rate
        if sample_bits not in (8, 16):
            raise ValueError("sample_bits must be 8 or 16")
        self._sample_bits = sample_bits
        if signed is None:
            self._signed = self._sample_bits != 8
        else:
            self._signed = bool(signed)
        self._little_endian = bool(little_endian)
        self._bytes_per_sample = self._sample_bits // 8
        self._max_value = (1 << self._sample_bits) - 1
        self._midpoint = 1 << (self._sample_bits - 1)
        self._duty_scale = 257 if self._sample_bits == 8 else 1
        chunk = int(chunk_size)
        if chunk < 128:
            chunk = 128
        remainder = chunk % self._bytes_per_sample
        if remainder:
            chunk += self._bytes_per_sample - remainder
        if chunk < self._bytes_per_sample:
            chunk = self._bytes_per_sample
        self._chunk_size = chunk
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

        if not self._refill():
            self.stop()
            return False

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
                sample = self._next_sample()
                duty = self._apply_volume(sample)
                self._pwm.duty_u16(duty)

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
        remainder = data_len % self._bytes_per_sample
        if remainder:
            data_len -= remainder
            if data_len <= 0:
                return False
            self._buffer[:data_len] = data[:data_len]
        else:
            self._buffer[:data_len] = data
        self._buf_len = data_len
        self._buf_pos = 0
        return True

    def _next_sample(self) -> int:
        if self._bytes_per_sample == 1:
            sample = self._buffer[self._buf_pos]
            self._buf_pos += 1
            if self._signed and sample >= 128:
                sample -= 256
            return sample

        start = self._buf_pos
        b0 = self._buffer[start]
        b1 = self._buffer[start + 1]
        self._buf_pos = start + 2
        if self._little_endian:
            value = b0 | (b1 << 8)
        else:
            value = (b0 << 8) | b1
        if self._signed:
            sign_bit = 1 << (self._sample_bits - 1)
            mask = (1 << self._sample_bits) - 1
            if value & sign_bit:
                value = value - (mask + 1)
        return value

    def _apply_volume(self, sample: int) -> int:
        if self._signed:
            centered = sample * self._volume
        else:
            centered = (sample - self._midpoint) * self._volume

        if centered >= 0:
            centered = int(centered + 0.5)
        else:
            centered = int(centered - 0.5)

        value = centered + self._midpoint

        if value < 0:
            value = 0
        elif value > self._max_value:
            value = self._max_value

        return value * self._duty_scale
