# ESP32 Audio TCP Server

TCP server for receiving audio streams from ESP32 devices. Supports MP3 conversion, transcription, and MQTT publishing.

## Features

- Concurrent connection handling (up to 4 simultaneous clients)
- Automatic MP3 conversion with FFmpeg
- Audio transcription using OpenAI-compatible API
- MQTT publishing for transcriptions
- Thread-safe operation with progress tracking
- Persistent data storage in `data/` subdirectory

## Quick Start with Docker

### Using Docker Compose (Recommended)

1. Build and start the server:
```bash
docker-compose up -d
```

2. View logs:
```bash
docker-compose logs -f
```

3. Stop the server:
```bash
docker-compose down
```

### Using Docker Directly

1. Build the image:
```bash
docker build -t esp32-audio-server .
```

2. Run the container:
```bash
docker run -d \
  --name esp32-audio-server \
  -p 8888:8888 \
  -v $(pwd)/data:/app/data \
  esp32-audio-server
```

3. View logs:
```bash
docker logs -f esp32-audio-server
```

4. Stop and remove:
```bash
docker stop esp32-audio-server
docker rm esp32-audio-server
```

## Configuration Options

### Configuration File (Recommended)

The server supports loading configuration from a YAML file. This is easier to manage than command-line arguments.

1. Create your configuration file:
```bash
cp config.example.yaml config.yaml
# Edit config.yaml with your settings
```

2. Run with Docker:
```bash
# Docker run
docker run -d \
  -v $(pwd)/config.yaml:/app/config.yaml:ro \
  -v $(pwd)/data:/app/data \
  -p 8888:8888 \
  esp32-audio-server --config /app/config.yaml

# Docker Compose (edit docker-compose.yml to uncomment config volume and command)
docker-compose up -d
```

Example `config.yaml`:
```yaml
server:
  host: 0.0.0.0
  port: 8888
  max_workers: 4

recording:
  mp3_enabled: true
  keep_raw: false

mqtt:
  broker: mqtt.example.com
  port: 1883
  username: user
  password: pass
```

**Note:** Command-line arguments override configuration file settings.

### Command-Line Options

```bash
# Custom port
docker run -p 9000:9000 esp32-audio-server --host 0.0.0.0 --port 9000

# Disable MP3 conversion (faster, saves CPU)
docker run esp32-audio-server --no-mp3

# Keep raw PCM files after MP3 conversion
docker run esp32-audio-server --keep-raw

# Custom output filename pattern
docker run esp32-audio-server --output recording.raw

# View all options
docker run esp32-audio-server --help
```

### MQTT Publishing

Enable MQTT to publish transcriptions to an MQTT broker:

```bash
docker run esp32-audio-server \
  --mqtt-broker mqtt.example.com \
  --mqtt-port 1883 \
  --mqtt-username user \
  --mqtt-password pass
```

Update `docker-compose.yml`:
```yaml
command: [
  "--host", "0.0.0.0",
  "--port", "8888",
  "--mqtt-broker", "mqtt.example.com",
  "--mqtt-username", "user",
  "--mqtt-password", "pass"
]
```

Transcriptions are published to: `sensors/transcription/text`

### Advanced Options

```bash
# Maximum concurrent connections
docker run esp32-audio-server --max-workers 8

# Single connection mode (exit after one connection)
docker run esp32-audio-server --single
```

## Firewall Rules

The application defaults to port 8888/TCP.  You must allow inboud connections on this port on the machine where this service runs.

A sample `ufw` firewall application rule is included in `stenographer.ufw`.  
To make use of this:
- `sudo cp stenographer.ufw /etc/ufw/applications.d/stenographer`
- `sudo ufw allow (from <subnet>/24) to any app "stenographer"`

## Data Directory

All audio files are saved to the `data/` subdirectory:
- Raw PCM files: `data/audio_YYYYMMDD_HHMMSS_N.raw`
- MP3 files: `data/audio_YYYYMMDD_HHMMSS_N.mp3`

When using Docker, mount the data directory for persistence:
```bash
-v $(pwd)/data:/app/data
```

## Local Development (Without Docker)

### Prerequisites

- Python 3.11+
- FFmpeg (for MP3 conversion)

### Installation

1. Install FFmpeg:
```bash
# Ubuntu/Debian
sudo apt-get install ffmpeg

# macOS
brew install ffmpeg
```

2. Install Python dependencies:
```bash
pip install -r requirements.txt
```

### Running the Server

```bash
python tcp_server.py --host 0.0.0.0 --port 8888
```

## Audio Format

The server expects raw PCM audio with these specifications:
- Sample rate: 48000 Hz
- Bit depth: 16-bit
- Channels: 1 (mono)
- Byte order: Little-endian

## Transcription Configuration

The server uses an OpenAI-compatible API for transcription. Configure in `tcp_server.py`:

```python
TRANSCRIPTION_TIMEOUT_S = 120
SPEACHES_BASE_URL = 'http://your-server:8000/v1/'
TRANSCRIPTION_MODEL_NAME = 'Systran/faster-whisper-large-v3'
```

## Troubleshooting

### Port Already in Use

```bash
# Find process using port 8888
lsof -i :8888

# Or change the port
docker run -p 9000:9000 esp32-audio-server --port 9000
```

### Permission Issues with Data Directory

```bash
# Create data directory with correct permissions
mkdir -p data
chmod 755 data
```

### FFmpeg Not Found

```bash
# Verify FFmpeg is installed in container
docker exec esp32-audio-server which ffmpeg
docker exec esp32-audio-server ffmpeg -version
```

### View Container Logs

```bash
# All logs
docker logs esp32-audio-server

# Follow logs
docker logs -f esp32-audio-server

# Last 100 lines
docker logs --tail 100 esp32-audio-server
```

## Performance

- Each connection runs in a separate thread
- Default: 4 concurrent connections (configurable with `--max-workers`)
- MP3 conversion happens after recording completes (non-blocking)
- Typical throughput: 96 KB/s per stream (768 kbps audio @ 48kHz 16-bit mono)

## Example Output

```
ESP32 Audio TCP Server
=====================
Listening on 0.0.0.0:8888
Mode: Continuous (Ctrl+C to stop)
Max concurrent connections: 4
MP3 conversion: Enabled
MQTT publishing: Disabled

Ready to accept connections...
Saving files to: /app/data/

Waiting for connection...

[ClientHandler-0] Connected by 192.168.1.100:54321
[ClientHandler-0] Receiving audio data to: data/audio_20250101_120000_1.raw
[ClientHandler-0] Received: 1,048,576 bytes | Duration: 5.5s | Rate: 187.5 KB/s
[ClientHandler-0] Total received: 1,048,576 bytes (1.00 MB)
[ClientHandler-0] Audio duration: 5.5 seconds
[ClientHandler-0] Transfer time: 5.6 seconds
[ClientHandler-0] Average rate: 187.5 KB/s
[ClientHandler-0] Raw audio saved to: data/audio_20250101_120000_1.raw
[ClientHandler-0] Converting to MP3: data/audio_20250101_120000_1.mp3
[ClientHandler-0] MP3 conversion successful!
[ClientHandler-0]   Raw size: 1,048,576 bytes (1.00 MB)
[ClientHandler-0]   MP3 size: 131,072 bytes (0.13 MB)
[ClientHandler-0]   Compression: 87.5% reduction
```

## Obsidian Integration

The server can publish transcriptions to MQTT, which can then be automatically appended to Obsidian daily notes.

### MQTT to Obsidian Bridge

A companion tool is available in the [mqtt_to_obsidian/](mqtt_to_obsidian/) directory that:
- Subscribes to MQTT transcription messages
- Automatically appends transcriptions to Obsidian daily notes with timestamps
- Supports direct file writing or Obsidian URI

See [mqtt_to_obsidian/README.md](mqtt_to_obsidian/README.md) for setup instructions.

### Quick Setup

1. Enable MQTT in the TCP server (see [CONFIG.md](CONFIG.md))
2. Configure and run the MQTT to Obsidian bridge:
```bash
cd mqtt_to_obsidian
cp bridge_config.example.yaml bridge_config.yaml
# Edit bridge_config.yaml with your vault path
python obsidian_mqtt_bridge.py --config bridge_config.yaml
```

### Data Flow

```
ESP32 → TCP Server → Transcription → MQTT → Obsidian Bridge → Daily Note
```

## License

This project is part of the ESP32 Audio Streamer system.
