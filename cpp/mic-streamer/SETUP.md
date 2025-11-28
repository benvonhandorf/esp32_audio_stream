# ESP32S3 Audio Streamer - Setup Guide

## Quick Start

### 1. Build and Flash the Configuration Tool

First, configure the device with WiFi credentials and TCP server settings:

```bash
# Enable configuration mode
idf.py menuconfig
# Navigate to: Audio Streamer Configuration -> Enable Configuration Mode -> [*]

# Build and flash
idf.py build flash monitor
```

Follow the prompts to enter:
- WiFi SSID
- WiFi Password
- TCP Server IP Address
- TCP Server Port (default: 8888)
- TCP Streaming Enable (y/n)

### 2. Build and Flash the Main Application

After configuration is saved:

```bash
# Disable configuration mode
idf.py menuconfig
# Navigate to: Audio Streamer Configuration -> Enable Configuration Mode -> [ ]

# Build and flash
idf.py build flash monitor
```

## Usage

### Recording Audio

1. **Press and hold** the button (GPIO 0) to start recording
2. **Release** the button to stop recording

While the button is held:
- Audio is captured from the PDM microphone at 48kHz, 16-bit, mono
- Audio is saved to SD card with a unique timestamped filename
- If WiFi and TCP are configured, audio is streamed to the TCP server in real-time

### Audio Files

Recorded audio files are saved to the SD card at:
```
/sdcard/audio_<timestamp>.raw
```

**File Format:**
- Raw PCM audio data
- 48000 Hz sample rate
- 16-bit signed integer samples
- Mono (1 channel)
- Little-endian byte order

**Convert to WAV:**
```bash
# Using ffmpeg
ffmpeg -f s16le -ar 48000 -ac 1 -i audio_1234567890.raw output.wav

# Using sox
sox -r 48000 -e signed -b 16 -c 1 audio_1234567890.raw output.wav
```

## TCP Server Setup

To receive audio streams, you need a TCP server listening on the configured port. Here's a simple Python example:

```python
#!/usr/bin/env python3
import socket
import sys

HOST = '0.0.0.0'  # Listen on all interfaces
PORT = 8888

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen(1)
    print(f'TCP server listening on {HOST}:{PORT}')

    conn, addr = s.accept()
    with conn:
        print(f'Connected by {addr}')
        with open('received_audio.raw', 'wb') as f:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                f.write(data)
                print(f'Received {len(data)} bytes')
        print('Connection closed')
```

Run the server:
```bash
python3 tcp_server.py
```

## Configuration Reference

Configuration is stored in NVS (non-volatile storage) and persists across reboots.

### Updating Configuration

To change WiFi or server settings, rebuild with configuration mode enabled:

```bash
idf.py menuconfig
# Enable: Audio Streamer Configuration -> Enable Configuration Mode

idf.py build flash monitor
```

### Manual NVS Configuration

You can also set configuration values manually using `nvs_set` commands in the ESP-IDF monitor:

| Key | Type | Description |
|-----|------|-------------|
| `wifi_ssid` | string | WiFi network SSID |
| `wifi_pass` | string | WiFi password |
| `server_addr` | string | TCP server IP address |
| `server_port` | uint16 | TCP server port (default: 8888) |
| `tcp_enabled` | uint8 | Enable TCP streaming (0=no, 1=yes) |

## Hardware Requirements

### M5 Cardputer v1.0

**Button:**
- GPIO 0 - Boot button (triggers recording)

**PDM Microphone:**
- GPIO 43 - Clock
- GPIO 46 - Data

**SD Card (SPI mode):**
- GPIO 40 - Clock
- GPIO 39 - MISO
- GPIO 14 - MOSI
- GPIO 12 - CS

### SD Card Preparation

1. Format the SD card as FAT32
2. Insert into the M5 Cardputer SD card slot
3. The device will automatically create audio files when recording

## Troubleshooting

### SD Card Not Detected

- Ensure SD card is formatted as FAT32
- Check that SD card is properly inserted
- Verify GPIO pins in [audio_streamer.h](main/audio_streamer.h)
- Check logs for "SD card mounted" message

### WiFi Connection Failed

- Verify SSID and password in configuration
- Check WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Look for "Got IP" message in logs
- Try re-running configuration mode

### TCP Connection Failed

- Ensure TCP server is running and listening
- Verify server IP address and port
- Check firewall settings on server
- Ensure device and server are on same network (or port forwarding is configured)

### No Audio Data

- Check PDM microphone is connected to correct GPIOs
- Verify I2S initialization in logs: "I2S PDM initialized successfully"
- Test microphone with a simple sound (speaking, tapping)
- Check buffer statistics in logs

### Audio Quality Issues

- Ensure SD card has sufficient write speed (Class 10 or higher recommended)
- Check for "queue full" warnings in logs (indicates buffer overflow)
- Verify 48kHz sample rate in configuration
- Test with shorter recordings first

## Advanced Configuration

### Adjusting Buffer Sizes

Edit [audio_streamer.h](main/audio_streamer.h):

```c
#define AUDIO_BUFFER_SIZE       4096   // Bytes per I2S read (increase for less frequent I/O)
#define AUDIO_BUFFER_COUNT      8      // Number of buffers (increase to handle bursts)
#define AUDIO_QUEUE_SIZE        16     // Queue depth (increase if seeing queue full warnings)
```

### Changing Audio Format

To change sample rate, edit [audio_streamer.h](main/audio_streamer.h):

```c
#define AUDIO_SAMPLE_RATE       48000  // Change to 16000, 32000, 44100, etc.
```

For stereo support, change:
```c
#define AUDIO_CHANNELS          2      // Stereo
```

And update I2S configuration in [audio_streamer.c](main/audio_streamer.c):
```c
.slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
    I2S_DATA_BIT_WIDTH_16BIT,
    I2S_SLOT_MODE_STEREO  // Change from MONO
),
```

## System Architecture

```
┌─────────────┐
│   Button    │ GPIO 0
│  (GPIO ISR) │
└──────┬──────┘
       │ triggers
       ▼
┌─────────────────────────────────────┐
│      State Machine                  │
│  IDLE → STARTING → RECORDING →      │
│         STOPPING → IDLE             │
└─────────────────────────────────────┘
       │
       ├──────────────────┬─────────────────┐
       ▼                  ▼                 ▼
┌─────────────┐    ┌──────────┐     ┌──────────┐
│  I2S PDM    │    │   WiFi   │     │ SD Card  │
│  48kHz Mono │    │  Client  │     │   SPI    │
└──────┬──────┘    └─────┬────┘     └─────┬────┘
       │                 │                 │
       ▼                 ▼                 ▼
┌─────────────────────────────────────────────┐
│          Audio Buffer Queue                 │
│     (Producer: Capture / Consumer: Writer)  │
└─────────────────────────────────────────────┘
       │
       ├──────────────────┬──────────────────┐
       ▼                  ▼                  ▼
┌─────────────┐    ┌──────────┐      ┌──────────┐
│   SD File   │    │   TCP    │      │   Logs   │
│  audio_*.raw│    │  Stream  │      │  Monitor │
└─────────────┘    └──────────┘      └──────────┘
```

### Task Structure

- **Main Task**: State machine management
- **Audio Capture Task** (Priority 10): Reads from I2S, fills buffers, queues for writing
- **Audio Writer Task** (Priority 9): Consumes buffers, writes to SD and TCP

## Performance Notes

**Expected Throughput:**
- Audio data rate: 48000 samples/sec × 2 bytes = 96 KB/sec
- Buffer fill rate: ~23 buffers/sec (at 4096 bytes per buffer)

**Memory Usage:**
- Static buffers: 8 × 4096 = 32 KB
- Queue overhead: ~256 bytes
- I2S DMA buffers: ~4 KB
- Total RAM: ~40 KB

**Latency:**
- I2S capture: <50ms
- SD write: <100ms (depends on card speed)
- TCP transmission: <50ms (depends on network)
- End-to-end: <200ms typical

## License

This project uses ESP-IDF which is licensed under Apache 2.0.
