/*
 * BioRhythm - Heartbeat Drum System
 * ESP32-based wearable that turns heartbeats into a rhythmic drum performance
 * streamed wirelessly to a Bluetooth A2DP speaker.
 *
 * Copyright (c) 2026
 * License: MIT
 *
 * DISCLAIMER: This is a creative / artistic project, NOT a medical device.
 * The "intense beat" indicator reflects raw signal amplitude only and does
 * NOT diagnose, screen for, or monitor any cardiac condition.
 */

#include <Arduino.h>
#include <BluetoothA2DPSource.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <FastLED.h>
#include <math.h>

// ----- Pin configuration -----
constexpr int PIEZO_PINS[4] = {34, 35, 32, 33};   // ADC1 channels (Wi-Fi safe)
constexpr int N_PIEZOS      = 4;
constexpr int LED_PIN       = 25;
constexpr int NUM_LEDS      = 30;

// ----- Bluetooth target -----
// IMPORTANT: ESP32 acts as the A2DP *source*. It must initiate the
// connection to a Bluetooth *sink* (speaker). Set this to your speaker's
// advertised name (visible in your phone's Bluetooth pairing list).
constexpr const char* BT_SPEAKER_NAME = "MyBluetoothSpeaker";

// ----- Audio constants -----
constexpr float SAMPLE_RATE_HZ = 44100.0f;
constexpr float BASS_FREQ_HZ   = 80.0f;
constexpr float RIM_FREQ_HZ    = 200.0f;
constexpr float ENVELOPE_DECAY = 8.0f;     // tau = 1/8 s ≈ 125 ms decay
constexpr float BASS_AMP       = 0.5f;
constexpr float RIM_AMP        = 0.3f;

// ----- Beat-detection constants -----
constexpr uint32_t MIN_BEAT_INTERVAL_MS = 400;   // ~150 BPM ceiling
constexpr uint32_t MAX_BEAT_INTERVAL_MS = 2000;  // ~30  BPM floor
constexpr int      BEAT_THRESHOLD_DELTA = 800;   // signal above adaptive baseline
constexpr float    BASELINE_ALPHA       = 0.95f; // EMA smoothing

// ----- Step-detection constants -----
constexpr uint32_t MIN_STEP_INTERVAL_MS = 250;
constexpr float    STEP_HIGH_THRESHOLD  = 1.5f;
constexpr float    STEP_LOW_THRESHOLD   = 0.5f;  // hysteresis lower bound

// ----- Globals -----
BluetoothA2DPSource a2dp_source;
Adafruit_MPU6050    mpu;
CRGB                leds[NUM_LEDS];

// Shared between loop() (core 1) and audio_callback (Bluetooth task).
// Aligned 32-bit reads/writes are atomic on Xtensa; volatile is sufficient
// for these single-word values.
volatile uint32_t last_beat_ms  = 0;
volatile int      beat_strength = 0;
volatile bool     intense_beat  = false;

float    heart_rate_bpm = 72.0f;
uint32_t step_count     = 0;

// Forward declaration
int32_t audio_callback(uint8_t* data, int32_t len);

static void halt_with_error_blink() {
    for (int i = 0; i < 5; i++) {
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        delay(300);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        delay(300);
    }
    ESP.restart();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    // LED bring-up first so we can show error states even if other init fails
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    fill_solid(leds, NUM_LEDS, CRGB::Blue);
    FastLED.show();

    // IMU
    if (!mpu.begin()) {
        Serial.println("MPU6050 init failed - restarting");
        halt_with_error_blink();
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    // Piezo inputs
    for (int i = 0; i < N_PIEZOS; i++) {
        pinMode(PIEZO_PINS[i], INPUT);
    }

    // A2DP source: ESP32 actively scans for and connects to the named speaker
    a2dp_source.set_auto_reconnect(true);
    a2dp_source.set_data_callback(audio_callback);
    a2dp_source.start(BT_SPEAKER_NAME);

    Serial.print("BioRhythm ready. Searching for ");
    Serial.println(BT_SPEAKER_NAME);
}

void loop() {
    const uint32_t now = millis();

    // -------------------------------------------------------------------
    // 1. Piezo beat detection with adaptive baseline + refractory period
    // -------------------------------------------------------------------
    static int  piezo_baseline   = 0;
    static bool baseline_seeded  = false;

    int piezo_sum = 0;
    for (int i = 0; i < N_PIEZOS; i++) {
        piezo_sum += analogRead(PIEZO_PINS[i]);
    }
    piezo_sum /= N_PIEZOS;

    if (!baseline_seeded) {
        piezo_baseline  = piezo_sum;
        baseline_seeded = true;
    }
    piezo_baseline = (int)(BASELINE_ALPHA * piezo_baseline +
                           (1.0f - BASELINE_ALPHA) * piezo_sum);
    const int signal = piezo_sum - piezo_baseline;

    if (signal > BEAT_THRESHOLD_DELTA &&
        (now - last_beat_ms) > MIN_BEAT_INTERVAL_MS) {

        const uint32_t interval = now - last_beat_ms;
        if (interval < MAX_BEAT_INTERVAL_MS) {
            heart_rate_bpm = 60000.0f / interval;
        }
        beat_strength = signal;
        intense_beat  = (signal > 2000);
        last_beat_ms  = now;  // 32-bit aligned store — safe to share

        Serial.printf("Beat @ %lu ms | signal=%d | HR=%.1f BPM\n",
                      (unsigned long)now, signal, heart_rate_bpm);
    }

    // -------------------------------------------------------------------
    // 2. Accelerometer step detection with debounce + hysteresis
    // -------------------------------------------------------------------
    static uint32_t last_step_ms    = 0;
    static bool     above_threshold = false;

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    const float ax = a.acceleration.x;
    const float ay = a.acceleration.y;
    const float az = a.acceleration.z;
    const float accel_dev = sqrtf(ax*ax + ay*ay + az*az) - 9.8f;
    const float accel_abs = fabsf(accel_dev);

    if (!above_threshold &&
        accel_abs > STEP_HIGH_THRESHOLD &&
        (now - last_step_ms) > MIN_STEP_INTERVAL_MS) {
        step_count++;
        last_step_ms    = now;
        above_threshold = true;
    } else if (accel_abs < STEP_LOW_THRESHOLD) {
        above_threshold = false;
    }

    // -------------------------------------------------------------------
    // 3. LED feedback (cosmetic only — NOT a health indicator)
    // -------------------------------------------------------------------
    CRGB base_color;
    if (now - last_beat_ms < 150) {
        base_color = intense_beat ? CRGB::Magenta : CRGB::Green;
    } else {
        base_color = CRGB::Blue;
    }
    fill_solid(leds, NUM_LEDS, base_color);
    const uint8_t fade_amount =
        (uint8_t)constrain(beat_strength / 20, 0, 255);
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i].fadeToBlackBy(fade_amount);
    }
    FastLED.show();

    // Telemetry (throttle to ~5 Hz to avoid flooding the serial monitor)
    static uint32_t last_print_ms = 0;
    if (now - last_print_ms > 200) {
        Serial.printf("HR:%.1f BPM | Strength:%d | Steps:%lu\n",
                      heart_rate_bpm, beat_strength,
                      (unsigned long)step_count);
        last_print_ms = now;
    }

    delay(20);  // ~50 Hz sensor loop
}

// ============================================================================
// A2DP audio callback - runs on the Bluetooth task.
//
// Buffer format: 16-bit signed stereo interleaved [L, R, L, R, ...].
// `len` is total bytes; frame count = len / 4
//   (2 channels × 2 bytes per int16_t).
// ============================================================================
int32_t audio_callback(uint8_t* data, int32_t len) {
    int16_t* audio = reinterpret_cast<int16_t*>(data);
    const int frames = len / 4;

    static uint32_t prev_seen_beat     = 0;
    static int      samples_since_beat = INT32_MAX;  // start silent

    const uint32_t current_beat = last_beat_ms;
    if (current_beat != prev_seen_beat) {
        prev_seen_beat     = current_beat;
        samples_since_beat = 0;
    }

    const bool rim_on = intense_beat;

    for (int i = 0; i < frames; i++) {
        const float t = samples_since_beat / SAMPLE_RATE_HZ;
        const float envelope = expf(-t * ENVELOPE_DECAY);

        float sample_f = 0.0f;
        if (envelope > 0.001f) {
            const float bass = sinf(2.0f * (float)M_PI * BASS_FREQ_HZ * t) * BASS_AMP;
            const float rim  = rim_on
                ? sinf(2.0f * (float)M_PI * RIM_FREQ_HZ * t) * RIM_AMP
                : 0.0f;
            sample_f = (bass + rim) * envelope;
        }

        // Clamp to [-1, 1] then scale to int16 with a touch of headroom
        if (sample_f >  1.0f) sample_f =  1.0f;
        if (sample_f < -1.0f) sample_f = -1.0f;
        const int16_t s = (int16_t)(sample_f * 30000.0f);

        audio[i * 2]     = s;   // Left
        audio[i * 2 + 1] = s;   // Right

        samples_since_beat++;
    }

    return len;
}
