## Overview

This project is a complete ESP32S3 audio streaming application that runs on the M5 Cardputer v1.0. It captures high-quality audio from a PDM microphone, displays real-time status on an LCD screen, streams to a TCP server, and saves to an SD card with automatic transcription support.

**Features:**
- ✅ Button-triggered recording (press to start, release to stop)
- ✅ High-quality audio capture: 48kHz, 16-bit, mono PCM from PDM microphone
- ✅ ST7789V2 LCD display with real-time status (WiFi, SD card space, recording stats)
- ✅ Dual storage: Real-time TCP streaming + SD card backup
- ✅ WiFi connectivity with hostname resolution support
- ✅ NVS-based configuration storage
- ✅ TCP server with MP3 encoding and Whisper transcription
- ✅ MQTT publishing of transcription results
- ✅ Comprehensive error handling and logging
- ✅ Modular architecture with separated display logic

## Quick Start

### 1. Initial Setup

```bash
# Configure WiFi and TCP server settings
idf.py menuconfig
# Enable: Audio Streamer Configuration -> Enable Configuration Mode

idf.py build flash monitor
# Use the interactive console to configure:
# - set_wifi <ssid> <password>
# - set_server <hostname|ip> <port>
# - set_tcp enable
# - save
# - restart
```

### 2. Build and Run

```bash
# Disable configuration mode
idf.py menuconfig
# Disable: Audio Streamer Configuration -> Enable Configuration Mode

idf.py build flash monitor
```

### 3. Start TCP Server

```bash
# Basic server (receives audio only)
./tools/tcp_server.py --port 8888

# With MP3 conversion
./tools/tcp_server.py --port 8888 --output recording.raw

# With transcription and MQTT publishing
./tools/tcp_server.py --port 8888 \
  --mqtt-broker mqtt.local \
  --mqtt-username user \
  --mqtt-password pass
```

### 4. Record Audio

- **Press and hold** GPIO 0 button to start recording
- Watch the LCD display for real-time status
- **Release** button to stop recording
- Audio is saved to SD card and streamed to TCP server (if configured)
- Server transcribes audio and publishes to MQTT topic `sensors/transcription/text`

**See [SETUP.md](SETUP.md) for detailed setup instructions and troubleshooting.**

## Project Status

✅ **All phases complete and implemented:**

- **Phase 1: Core Audio & Control**
  - ✅ 48kHz audio sample rate
  - ✅ Circular audio buffer with ring buffer system (16 buffers × 4KB)
  - ✅ Button GPIO interrupt handler
  - ✅ State machine (IDLE/STARTING/RECORDING/STOPPING)
  - ✅ Optimized task priorities (Writer: 10, Capture: 9, Display: 5)

- **Phase 2: Local Storage**
  - ✅ SD card initialization (SPI mode)
  - ✅ Audio file writing with unique timestamped filenames
  - ✅ SD card error handling and cleanup
  - ✅ Optimized buffering (64KB file buffer, fflush every ~1MB)

- **Phase 3: Network Streaming**
  - ✅ WiFi stack with NVS configuration
  - ✅ TCP client for real-time audio streaming
  - ✅ DNS resolution support (hostnames + IP addresses)
  - ✅ Network error handling and reconnection

- **Phase 4: Display & UI**
  - ✅ ST7789V2 LCD driver (240×135 pixels)
  - ✅ Custom 5×7 bitmap font rendering
  - ✅ Real-time status display (WiFi, SD card space, recording stats)
  - ✅ Modular display code in separate file
  - ✅ RGB565 color support

- **Phase 5: Server & Transcription**
  - ✅ Python TCP server with concurrent connection handling
  - ✅ MP3 encoding with pydub/ffmpeg
  - ✅ Whisper transcription integration
  - ✅ MQTT publishing to `sensors/transcription/text`
  - ✅ Configurable transcription service (OpenAI-compatible API)

- **Phase 6: Polish & Optimization**
  - ✅ NVS configuration management
  - ✅ Interactive configuration tool via USB Serial/JTAG
  - ✅ Comprehensive error handling
  - ✅ Performance optimizations (task priorities, buffering)
  - ✅ Helper scripts and documentation
  - ✅ Modular code architecture

## Hardware

**Platform:** M5 Cardputer v1.0 (ESP32S3)

### Button
- GPIO 0 - Boot button (triggers recording)

### Microphone
PDM Microphone
- Clock - GPIO 43
- Data - GPIO 46

### SD Card
SPI mode on dedicated bus
- Clock - GPIO 40
- MISO - GPIO 39
- MOSI - GPIO 14
- CS - GPIO 12

### Display
ST7789V2 LCD (240×135 pixels)
- MOSI - GPIO 35
- SCK - GPIO 36
- CS - GPIO 37
- DC - GPIO 34
- RST - GPIO 33
- Backlight - GPIO 38

## File Structure

```
mic-streamer/
├── main/
│   ├── audio_streamer.h      # Main application header
│   ├── audio_streamer.c      # Core audio and networking logic
│   ├── display.h             # Display module header
│   ├── display.c             # ST7789V2 LCD driver and UI
│   ├── config_tool.c         # Configuration utility
│   ├── main.c                # Entry point
│   ├── CMakeLists.txt        # Build configuration
│   └── Kconfig.projbuild     # Menu configuration
├── tools/
│   ├── tcp_server.py         # Python TCP server with transcription & MQTT
│   └── convert_to_wav.sh     # Raw PCM to WAV converter
├── README.md                 # This file
└── SETUP.md                  # Detailed setup guide
```

## Tools

### TCP Server

The Python TCP server supports multiple concurrent connections, MP3 encoding, Whisper transcription, and MQTT publishing.

**Basic usage:**
```bash
# Receive audio only
./tools/tcp_server.py --port 8888 --output recording.raw

# Single connection mode
./tools/tcp_server.py --port 8888 --single

# Keep raw files after MP3 conversion
./tools/tcp_server.py --port 8888 --keep-raw

# Disable MP3 conversion
./tools/tcp_server.py --port 8888 --no-mp3

# With MQTT publishing
./tools/tcp_server.py --port 8888 \
  --mqtt-broker primemover.local \
  --mqtt-port 1883 \
  --mqtt-username user \
  --mqtt-password pass

# Maximum concurrent connections
./tools/tcp_server.py --port 8888 --max-workers 8
```

**Server features:**
- Concurrent connection handling with ThreadPoolExecutor
- Automatic MP3 encoding with configurable quality (192kbps)
- Whisper transcription via OpenAI-compatible API
- MQTT publishing to `sensors/transcription/text` topic
- Connection tracking with unique IDs
- Progress display with data rate and duration

**Dependencies:**
```bash
pip install pydub paho-mqtt openai
# Also requires ffmpeg for MP3 encoding
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

The application uses a multi-task architecture with FreeRTOS:

### Task Structure

1. **Main Task** (Priority: default)
   - Manages application state based on button events
   - Handles state transitions (IDLE → STARTING → RECORDING → STOPPING)

2. **Audio Capture Task** (Priority: 9)
   - Continuously reads from I2S PDM interface
   - Fills ring buffers (16 buffers × 4KB each)
   - Queues buffers for writer task
   - Handles queue overflow with 100ms timeout

3. **Audio Writer Task** (Priority: 10, highest)
   - Consumes queued audio buffers
   - Writes to SD card with 64KB file buffer
   - Streams to TCP socket in parallel
   - Flushes every ~1MB (256 writes) to optimize SD card performance
   - Handles write errors and SD card failures

4. **Display Task** (Priority: 5, lowest)
   - Updates LCD screen every 500ms
   - Shows WiFi status, SD card space, recording stats
   - Queries SD card space via FATFS API
   - Non-blocking to avoid interfering with audio

### Data Flow

```
PDM Mic → I2S → Ring Buffer → Queue → [SD Card + TCP Socket]
                                    ↓
                               MP3 Encode → Transcribe → MQTT Publish
```

### Synchronization

- State transitions: Protected by mutex (`state_mutex`)
- Audio data: Lock-free queue with FreeRTOS `xQueue` primitives
- Display updates: Non-blocking, queries state atomically

## Configuration

Settings are stored in NVS and persist across reboots. Use the interactive console (when configuration mode is enabled) or configure manually.

### Interactive Commands

```
config> set_wifi <ssid> <password>      # Configure WiFi credentials
config> set_server <hostname|ip> <port> # Set TCP server (supports DNS)
config> set_tcp <enable|disable>        # Enable/disable TCP streaming
config> show                            # Display current settings
config> save                            # Save to NVS flash
config> restart                         # Reboot device
```

### NVS Storage

| Setting | Description | Default |
|---------|-------------|---------|
| `wifi_ssid` | WiFi network name | (empty) |
| `wifi_pass` | WiFi password | (empty) |
| `server_addr` | TCP server hostname or IP | (empty) |
| `server_port` | TCP server port | 8888 |
| `tcp_enabled` | Enable TCP streaming | false |

## Audio Format

**Captured Format:**
- Sample rate: 48000 Hz
- Bit depth: 16-bit signed integer
- Channels: 1 (mono)
- Byte order: Little-endian
- Container: Raw PCM (.raw files)

**Encoded Format (server-side):**
- Format: MP3
- Bitrate: 192 kbps
- Compression: ~85-90% size reduction
- Quality: High (ffmpeg `-q:a 0`)

## Performance

**Audio Pipeline:**
- **Data rate**: 96 KB/sec (48000 samples/sec × 2 bytes)
- **Latency**: <200ms end-to-end (typical)
- **Buffer memory**: 64 KB (16 buffers × 4KB)
- **File buffer**: 64 KB (optimized for SD card)
- **Queue depth**: 32 buffers

**Storage:**
- **Raw PCM**: ~5.76 MB per minute (96 KB/sec × 60)
- **MP3 (192kbps)**: ~1.44 MB per minute (~75% smaller)

**Memory Usage:**
- Audio buffers: 64 KB
- LCD framebuffer: 64.8 KB (240×135×2 bytes)
- File buffer: 64 KB
- Stack (3 tasks): 20 KB (8KB + 8KB + 4KB)
- **Total**: ~213 KB RAM

**Task Stack Sizes:**
- Audio Capture: 8192 bytes
- Audio Writer: 8192 bytes (increased for FAT filesystem calls)
- Display: 4096 bytes

## Display Information

The ST7789V2 LCD shows real-time status:

**Screen Layout (240×135 pixels):**
```
ESP32 Audio Streamer
WiFi: Connected/Disconnected
SD: 1234/4096MB
Status: RECORDING / Idle
Time: 0:42
Size: 4096KB
TCP: Active
Server: hostname.local:8888
```

**Color Coding:**
- Cyan: Title text
- Green: Active/connected status
- Red: Recording indicator, errors
- Yellow: Recording statistics
- Gray: Idle state messages
- White: Labels and server info

## MQTT Integration

The TCP server publishes transcription results to MQTT:

**Topic:** `sensors/transcription/text`

**Payload format (JSON):**
```json
{
  "text": "transcribed text from audio",
  "timestamp": "2025-11-28T12:34:56.789",
  "file": "audio_20251128_123456_1.mp3",
  "language": "en"
}
```

**QoS:** 1 (at least once delivery)

## Transcription Service

The server uses an OpenAI-compatible API for transcription:

**Configuration (in tcp_server.py):**
```python
SPEACHES_BASE_URL = 'http://primemover.local:8000/v1/'
TRANSCRIPTION_MODEL_NAME = 'Systran/faster-whisper-large-v3'
TRANSCRIPTION_TIMEOUT_S = 120
```

**Supported services:**
- OpenAI Whisper API
- Local Whisper servers (faster-whisper, whisper.cpp)
- Any OpenAI-compatible transcription endpoint

## Troubleshooting

### Audio Queue Full
If you see "Audio queue full, dropping buffer" warnings:
- SD card write speed is too slow
- Check SD card class (Class 10 recommended)
- Filesystem may be fragmented
- Solution: Already optimized with 64KB file buffer and fflush every ~1MB

### Spinlock Crashes
If device crashes during recording:
- Stack overflow in writer task
- Solution: Already fixed with 8KB stack size

### Display Issues
- **Upside down**: Adjust `esp_lcd_panel_mirror()` parameters in display.c
- **Wrong colors**: Check RGB endianness setting
- **No backlight**: Verify GPIO 38 configuration

### Network Issues
- **WiFi not connecting**: Check SSID/password in NVS
- **DNS fails**: Ensure server hostname is resolvable
- **TCP timeout**: Check server is running and port is open

## Development

### Code Organization

The codebase follows a modular architecture:

- **audio_streamer.c**: Core audio capture, SD card, network, state management
- **display.c**: LCD driver, font rendering, UI updates, SD space queries
- **config_tool.c**: Interactive configuration console
- **main.c**: Application entry point and initialization

### Key Design Decisions

1. **Task Priorities**: Writer (10) > Capture (9) > Display (5) to prevent audio drops
2. **Buffering Strategy**: Large file buffer (64KB) with infrequent fflush (~1MB) for SD card performance
3. **Display Separation**: Isolated in separate module for maintainability
4. **SD Space Queries**: Use FATFS native API (`f_getfree`) instead of POSIX `statvfs`
5. **Font Rendering**: Custom 5×7 bitmap font for minimal memory footprint
6. **Error Handling**: Graceful degradation (continues without SD card/WiFi if unavailable)

## Documentation

- **[SETUP.md](SETUP.md)** - Complete setup guide, troubleshooting, and advanced configuration
- **Code comments** - Inline documentation in all source files
- **display.h** - Display module API and color definitions
- **audio_streamer.h** - Main application API and hardware configuration

## License

This project builds on ESP-IDF examples which are licensed under Apache 2.0 / CC0.
