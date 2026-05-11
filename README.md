# BioRhythm

**BioRhythm** is an ESP32-based wearable that transforms a live heartbeat
signal into a synthesised drum performance, streamed wirelessly over
Bluetooth A2DP to any speaker.

> ⚠️ **Disclaimer**
> BioRhythm is a creative / artistic project. It is **not** a medical device
> and must not be used to diagnose, screen for, or monitor any cardiac
> condition. The "intense beat" indicator reflects raw piezo signal
> amplitude only.

---

## Features

- **Heart-driven percussion** — piezoelectric sensors detect chest-wall
  vibrations; each beat triggers a synthesised kick drum.
- **Motion-aware** — onboard MPU6050 tracks steps and orientation.
- **Visual feedback** — WS2812B LED strip pulses with each detected beat.
- **Wireless audio** — ESP32 acts as an A2DP source and streams 16-bit
  stereo PCM to a paired Bluetooth speaker.

---

## Hardware

| Component                         | Quantity |
| --------------------------------- | -------- |
| ESP32 development board           | 1        |
| MPU6050 accelerometer + gyroscope | 1        |
| Piezoelectric vibration sensor    | 4        |
| WS2812B LED strip (30 LEDs)       | 1        |
| Bluetooth A2DP speaker            | 1        |

## Pin configuration

| Function          | ESP32 pin            |
| ----------------- | -------------------- |
| Piezo 1–4 (ADC1)  | 34, 35, 32, 33       |
| WS2812B data      | 25                   |
| MPU6050 SDA / SCL | 21 / 22 (default I²C)|

> The piezo channels use **ADC1** specifically. ADC2 pins (e.g. 4, 12–15, 25–27)
> are unreliable when Wi-Fi or Bluetooth is active.

---

## Setup

1. Clone the repository:
   ```bash
   git clone https://github.com/<your-user>/BioRhythm.git
   cd BioRhythm
   ```

2. Open the folder in **VS Code with the PlatformIO extension**.

3. **Set your Bluetooth speaker's name** in `src/main.cpp`:
   ```cpp
   constexpr const char* BT_SPEAKER_NAME = "MyBluetoothSpeaker";
   ```
   Replace `MyBluetoothSpeaker` with the exact name your speaker advertises
   (visible in your phone's Bluetooth pairing list). The ESP32 acts as the
   A2DP *source* and will scan for and connect to that sink on boot.

4. Power the speaker on **before** flashing so the ESP32 can discover it
   on first boot. `auto_reconnect` is enabled, so subsequent power-ons
   should reconnect automatically.

5. Build and upload:
   ```bash
   pio run -t upload
   pio device monitor
   ```

---

## How it works

```
   Piezo array (×4)      MPU6050
        │                    │
        ▼                    ▼
  ADC sampling          Accel events
        │                    │
        ▼                    ▼
   Adaptive baseline     Step debounce
   + threshold + refractory
        │
        ├─────────────► last_beat_ms (volatile, 32-bit atomic)
        │
        ▼                                  ▲
   LED feedback (FastLED)                  │
                                           │
                                  ┌────────┴────────┐
                                  │ A2DP audio task │
                                  │ on BT core      │
                                  └────────┬────────┘
                                           │
                              For each frame in callback:
                                t = samples_since_beat / 44.1 kHz
                                envelope = exp(-t · 8)
                                sample   = sin(2π · 80 · t) · envelope
                                           │
                                           ▼
                                 16-bit stereo PCM → A2DP sink
```

Beat detection runs an exponential moving average over the piezo signal,
then thresholds the deviation against an adaptive baseline. A 400 ms
refractory window prevents double-triggering. The detected timestamp is
shared with the audio callback via a `volatile uint32_t` — aligned 32-bit
loads/stores are atomic on Xtensa, so no mutex is required for this
single-word state.

The audio callback re-evaluates the synthesis envelope on every sample
based on the elapsed time since the most recent beat, generating a clean
exponentially-decaying sine that lasts roughly 500 ms before falling
below 16-bit resolution.

---

## Calibration

The two values most worth tuning for your build:

| Constant                   | Effect                                                 |
| -------------------------- | ------------------------------------------------------ |
| `BEAT_THRESHOLD_DELTA`     | Higher = fewer false beats, may miss soft heartbeats   |
| `BASELINE_ALPHA`           | Closer to 1.0 = slower adaptation, more stable baseline|

Open the serial monitor at 115200 baud, watch the `signal=` value, and
set `BEAT_THRESHOLD_DELTA` to roughly 1.5–2× the resting noise floor.

---

## Troubleshooting

| Symptom                              | Likely cause                                              |
| ------------------------------------ | --------------------------------------------------------- |
| No audio at all                      | `BT_SPEAKER_NAME` doesn't match your speaker exactly      |
| Build error: `main.cpp` not found    | Source must be at `src/main.cpp`, not repo root           |
| Audio is silent or crackles          | Confirm sensor wiring; check Serial for buffer warnings   |
| Beats trigger constantly             | Lower piezo gain or increase `BEAT_THRESHOLD_DELTA`       |
| LEDs flash red and ESP32 reboots     | MPU6050 not detected on I²C — check SDA/SCL and power     |

---

## License

MIT — see [LICENSE](LICENSE).
