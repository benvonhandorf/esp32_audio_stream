## Overview

This project is a complete ESP32S3 audio streaming application that runs on the M5 Cardputer v1.0.

**Features:**
- ✅ Button-triggered recording (press to start, release to stop)
- ✅ High-quality audio capture: 48kHz, 16-bit, mono PCM from PDM microphone
- ✅ Dual storage: Real-time TCP streaming + SD card backup
- ✅ WiFi connectivity with automatic reconnection
- ✅ NVS-based configuration storage
- ✅ Low power idle mode
- ✅ Comprehensive error handling and logging

## Quick Start

### 1. Initial Setup

```bash
# Configure WiFi and TCP server settings
idf.py menuconfig
# Enable: Audio Streamer Configuration -> Enable Configuration Mode

idf.py build flash monitor
# Follow prompts to enter WiFi SSID, password, and server details
```

### 2. Build and Run

```bash
# Disable configuration mode
idf.py menuconfig
# Disable: Audio Streamer Configuration -> Enable Configuration Mode

idf.py build flash monitor
```

### 3. Record Audio

- **Press and hold** GPIO 0 button to start recording
- **Release** button to stop recording
- Audio is saved to SD card and streamed to TCP server (if configured)

**See [SETUP.md](SETUP.md) for detailed setup instructions and troubleshooting.**

## Project Status

✅ **All phases complete and implemented:**

- **Phase 1: Core Audio & Control**
  - ✅ 48kHz audio sample rate (upgraded from 16kHz)
  - ✅ Circular audio buffer for real-time streaming
  - ✅ Button GPIO interrupt handler
  - ✅ State machine (IDLE/STARTING/RECORDING/STOPPING)

- **Phase 2: Local Storage**
  - ✅ SD card initialization (SPI mode)
  - ✅ Audio file writing with unique timestamped filenames
  - ✅ SD card error handling and cleanup

- **Phase 3: Network Streaming**
  - ✅ WiFi stack with NVS configuration
  - ✅ TCP client for real-time audio streaming
  - ✅ Network error handling and reconnection

- **Phase 4: Polish**
  - ✅ NVS configuration management
  - ✅ Interactive configuration tool
  - ✅ Comprehensive error handling
  - ✅ Helper scripts and documentation

## Hardware

**Platform:** M5 Cardputer v1.0

### Button
- GPIO 0 - Boot button (triggers recording)

### Microphone
PDM Microphone
- Clock - GPIO 43
- Data - GPIO 46

### SD Card
SPI mode
- Clock - GPIO 40
- MISO - GPIO 39
- MOSI - GPIO 14
- CS - GPIO 12

## File Structure

```
mic-streamer/
├── main/
│   ├── audio_streamer.h      # Main application header
│   ├── audio_streamer.c      # Complete implementation
│   ├── config_tool.c         # Configuration utility
│   ├── main.c                # Entry point
│   ├── CMakeLists.txt        # Build configuration
│   └── Kconfig.projbuild     # Menu configuration
├── tools/
│   ├── tcp_server.py         # Python TCP server for receiving audio
│   └── convert_to_wav.sh     # Raw PCM to WAV converter
├── README.md                 # This file
└── SETUP.md                  # Detailed setup guide
```

## Tools

### TCP Server
Receive audio streams from the device:
```bash
./tools/tcp_server.py --port 8888 --output recording.raw
```

### Audio Conversion
Convert raw PCM to WAV format:
```bash
./tools/convert_to_wav.sh audio_1234567890.raw output.wav
```

Or use ffmpeg directly:
```bash
ffmpeg -f s16le -ar 48000 -ac 1 -i audio.raw output.wav
```

## Architecture

The application uses a producer-consumer pattern with FreeRTOS:

1. **Main Task**: Manages application state based on button events
2. **Audio Capture Task** (High priority): Continuously reads from I2S PDM, fills circular buffers, queues them for writing
3. **Audio Writer Task**: Consumes queued buffers, writes to SD card and TCP socket

All state transitions are mutex-protected. Audio data flows through a lock-free queue from capture to writer tasks.

## Configuration

Settings are stored in NVS and persist across reboots:

| Setting | Description | Default |
|---------|-------------|---------|
| `wifi_ssid` | WiFi network name | (empty) |
| `wifi_pass` | WiFi password | (empty) |
| `server_addr` | TCP server IP address | (empty) |
| `server_port` | TCP server port | 8888 |
| `tcp_enabled` | Enable TCP streaming | false |

## Audio Format

**Captured Format:**
- Sample rate: 48000 Hz
- Bit depth: 16-bit signed integer
- Channels: 1 (mono)
- Byte order: Little-endian
- Container: Raw PCM (.raw files)

## Performance

- **Data rate**: 96 KB/sec (48000 samples/sec × 2 bytes)
- **Latency**: <200ms end-to-end (typical)
- **Memory**: ~40 KB RAM for buffers
- **Storage**: ~5.8 MB per minute of audio

## Documentation

- **[SETUP.md](SETUP.md)** - Complete setup guide, troubleshooting, and advanced configuration
- **Code comments** - Inline documentation in all source files

## License

This project builds on ESP-IDF examples which are licensed under Apache 2.0 / CC0.
