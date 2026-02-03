/*
 * PulseEcho - Heartbeat Drum System
 * Copyright (c) 2026
 * License: MIT
 */

#include <WiFi.h>
#include <BluetoothA2DPSource.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <FastLED.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

// Pins
#define PIEZO1_PIN 34
#define PIEZO2_PIN 35
#define PIEZO3_PIN 32
#define PIEZO4_PIN 33
#define LED_PIN 25
#define NUM_LEDS 30

// Globals
BluetoothA2DPSource a2dp_source;
Adafruit_MPU6050 mpu;
CRGB leds[NUM_LEDS];

float heart_rate = 72;  // BPM estimate
int beat_strength = 0;
int step_count = 0;
volatile unsigned long last_beat = 0; // Volatile for shared access
bool healthy_rhythm = true;

// Simple drum samples (sine waves for bass/rim)
float bass_freq = 80;   // Hz
float rim_freq = 200;

// Forward declaration
int32_t audio_callback(uint8_t *data, int32_t len);

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);

  if (!mpu.begin()) { Serial.println("MPU6050 failed"); while(1); }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  a2dp_source.set_name("PulseEcho");
  // Register callback ONCE in setup
  a2dp_source.set_data_callback(audio_callback);
  a2dp_source.start("PulseEcho");

  Serial.println("PulseEcho - Heartbeat Drum System Ready!");
}

void loop() {
  // 1. Read piezos for heart sounds (20-200Hz envelope)
  int piezo_sum = 0;
  piezo_sum += analogRead(PIEZO1_PIN);
  piezo_sum += analogRead(PIEZO2_PIN);
  piezo_sum += analogRead(PIEZO3_PIN);
  piezo_sum += analogRead(PIEZO4_PIN);
  piezo_sum /= 4;

  // Detect beat (threshold + timing)
  if (piezo_sum > 1000 && millis() - last_beat > 400) {  // ~150BPM max
    beat_strength = piezo_sum - 1000;
    heart_rate = 60000.0 / (millis() - last_beat);
    last_beat = millis();

    // Murmur check: high variance or extra peaks (simplified)
    healthy_rhythm = (beat_strength < 2000);  // Tune based on tests
    
    // Note: No need to call generateDrumSound() manually; 
    // updating last_beat triggers the logic in the callback.
    Serial.println("Beat detected at " + String(last_beat));
  }

  // 2. Read accel for steps
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float accel_mag = sqrt(a.acceleration.x*a.acceleration.x + 
                         a.acceleration.y*a.acceleration.y + 
                         a.acceleration.z*a.acceleration.z) - 9.8;  // Gravity norm
  if (abs(accel_mag) > 1.5) {  // Step threshold
    step_count++;
  }

  // 3. Update LEDs: green=healthy sync, red=issue
  int health_score = (healthy_rhythm ? 1 : 0) + (abs(accel_mag) < 3 ? 1 : 0);  // 0-2
  CRGB color = (health_score == 2) ? CRGB::Green : (health_score == 1 ? CRGB::Yellow : CRGB::Red);
  fill_solid(leds, NUM_LEDS, color);
  for(int i=0; i<NUM_LEDS; i++) leds[i].fadeToBlackBy(beat_strength/10);
  FastLED.show();

  Serial.printf("HR:%.1f BPM, Strength:%d, Steps:%d, Health:%d\n", heart_rate, beat_strength, step_count, health_score);
  delay(50);
}

// A2DP callback: generate drum audio
int32_t audio_callback(uint8_t *data, int32_t len) {
  static float phase_bass = 0, phase_rim = 0;
  int16_t *audio_data = (int16_t*)data;
  int samples = len / 2;

  for(int i=0; i<samples; i++) {
    if (millis() - last_beat < 100) {  // Beat envelope
      float bass = 0.3 * sin(phase_bass * 2*PI * bass_freq / 44100) * exp(-(i/44100.0)*5);
      float rim = 0.2 * sin(phase_rim * 2*PI * rim_freq / 44100) * exp(-(i/44100.0)*10);
      float sample = (bass + (!healthy_rhythm ? rim*2 : 0)) * 30000;  // 16-bit scale
      audio_data[i*2] = audio_data[i*2+1] = sample;  // Mono
    } else {
      audio_data[i*2] = audio_data[i*2+1] = 0;
    }
    phase_bass += 1.0/44100;
    phase_rim += 1.0/44100;
  }
  return len;
}

// Removed generateDrumSound since it's now handled by the continuous callback loop checking last_beat
