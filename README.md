# PulseEcho

**PulseEcho** is an ESP32-based wearable that transforms your living heartbeat into a rhythmic drum performance, streamed wirelessly to any Bluetooth speaker.

## Features
-   **Live Heart Resonation**: Piezoelectric sensors capture the raw acoustic signature of your heart.
-   **Motion Sync**: Integrated MPU6050 accelerometer to weave your movement into the beat.
-   **Visual Pulse**: LED strip breathes and pulses in sync with your rhythm.
-   **Wireless Thump**: Generates synthesized kick drums and rimshots, streaming them directly to A2DP Bluetooth speakers.

## Hardware Required
-   ESP32 Development Board
-   MPU6050 Accelerometer/Gyroscope
-   Piezo Vibration Sensors (x4 array recommended)
-   WS2812B LED Strip (connected to Pin 25)
-   Bluetooth A2DP Speaker

## Pin Configuration
| Component | Pin |
|Data|34, 35, 32, 33|
|LED Strip|25|
|MPU6050|SDA (21), SCL (22)|

## Setup
1.  Clone this repository.
2.  Open in VS Code with PlatformIO extension.
3.  Build and upload to your ESP32.
4.  The device will start advertising as "**PulseEcho**". Connect your Bluetooth speaker to it.

## License
MIT
