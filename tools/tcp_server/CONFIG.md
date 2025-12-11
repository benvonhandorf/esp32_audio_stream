# Configuration File Guide

The ESP32 Audio TCP Server supports configuration via YAML files, making it easier to manage settings compared to long command-line arguments.

## Quick Start

1. Copy the example configuration:
   ```bash
   cp config.example.yaml config.yaml
   ```

2. Edit `config.yaml` with your settings

3. Run the server:
   ```bash
   # Local Python
   python tcp_server.py --config config.yaml

   # Docker
   docker run -v $(pwd)/config.yaml:/app/config.yaml:ro \
     esp32-audio-server --config /app/config.yaml
   ```

## Configuration Structure

### Server Section

Controls the TCP server behavior:

```yaml
server:
  host: 0.0.0.0          # Bind address (0.0.0.0 = all interfaces)
  port: 8888             # TCP port to listen on
  max_workers: 4         # Maximum concurrent connections
```

### Recording Section

Controls audio recording and conversion:

```yaml
recording:
  output_pattern: ""     # Custom filename pattern (empty = default timestamp)
  mp3_enabled: true      # Convert to MP3 (requires ffmpeg)
  keep_raw: false        # Keep .raw files after MP3 conversion
  single_connection: false  # Exit after first connection (testing mode)
```

### MQTT Section

Controls MQTT publishing for transcriptions:

```yaml
mqtt:
  broker: ""            # MQTT broker hostname (empty = disabled)
  port: 1883            # MQTT broker port
  username: ""          # Optional authentication
  password: ""          # Optional authentication
```

## Priority Order

Configuration values are resolved in this order (highest to lowest priority):

1. **Command-line arguments** (highest priority)
2. **Configuration file values**
3. **Built-in defaults** (lowest priority)

### Example Priority

Given this `config.yaml`:
```yaml
server:
  port: 9000
```

And this command:
```bash
python tcp_server.py --config config.yaml --port 8888
```

The server will use **port 8888** (command-line overrides config file).

## Use Cases

### Production Server

```yaml
server:
  host: 0.0.0.0
  port: 8888
  max_workers: 8

recording:
  mp3_enabled: true
  keep_raw: false

mqtt:
  broker: mqtt.production.com
  port: 1883
  username: audio_server
  password: secret123
```

### Development/Testing

```yaml
server:
  host: 127.0.0.1      # Local only
  port: 8888
  max_workers: 2       # Fewer workers

recording:
  mp3_enabled: false   # Faster, no conversion
  keep_raw: true
  single_connection: true  # Exit after one recording

mqtt:
  broker: ""           # Disabled
```

### High-Performance Setup

```yaml
server:
  host: 0.0.0.0
  port: 8888
  max_workers: 16      # Many concurrent streams

recording:
  mp3_enabled: false   # Skip conversion for speed
  keep_raw: true
```

### Storage-Optimized Setup

```yaml
server:
  host: 0.0.0.0
  port: 8888
  max_workers: 4

recording:
  mp3_enabled: true    # Compress to MP3
  keep_raw: false      # Delete raw files
```

## Docker Integration

### Method 1: Mount Config File

```bash
docker run -d \
  --name esp32-audio \
  -v $(pwd)/config.yaml:/app/config.yaml:ro \
  -v $(pwd)/data:/app/data \
  -p 8888:8888 \
  esp32-audio-server --config /app/config.yaml
```

### Method 2: Docker Compose

Edit `docker-compose.yml`:

```yaml
services:
  tcp-server:
    # ... other settings ...
    volumes:
      - ./config.yaml:/app/config.yaml:ro
      - ./data:/app/data
    command: ["--config", "/app/config.yaml"]
```

Then run:
```bash
docker-compose up -d
```

## Validation

The server will:
- Warn if the config file doesn't exist (uses defaults)
- Exit with error if the config file has invalid YAML syntax
- Ignore unknown configuration keys

## Environment-Specific Configs

You can maintain multiple configuration files:

```bash
# Development
python tcp_server.py --config config.dev.yaml

# Staging
python tcp_server.py --config config.staging.yaml

# Production
python tcp_server.py --config config.prod.yaml
```

## Security Notes

### Sensitive Data

Configuration files may contain sensitive data (MQTT passwords, etc.):

1. **Never commit `config.yaml` to git**
   - Only commit `config.example.yaml`
   - Add `config.yaml` to `.gitignore`

2. **Use file permissions**
   ```bash
   chmod 600 config.yaml  # Owner read/write only
   ```

3. **Use read-only mounts in Docker**
   ```bash
   -v $(pwd)/config.yaml:/app/config.yaml:ro
   ```

### Alternative: Environment Variables

For sensitive values, consider using command-line args with environment variables:

```bash
python tcp_server.py \
  --config config.yaml \
  --mqtt-password "${MQTT_PASSWORD}"
```

## Troubleshooting

### Config file not found

```
Warning: Config file 'config.yaml' not found, using defaults
```

**Solution**: Check the file path. Use absolute path or ensure you're in the correct directory.

### YAML syntax error

```
Error parsing config file: ...
```

**Solution**: Validate your YAML syntax. Common issues:
- Inconsistent indentation (use spaces, not tabs)
- Missing colons after keys
- Unquoted special characters

Use an online YAML validator or:
```bash
python -c "import yaml; yaml.safe_load(open('config.yaml'))"
```

### Configuration not applied

**Solution**: Remember command-line args override config file. Check:
```bash
python tcp_server.py --config config.yaml --help
```

## Complete Example

Here's a complete production-ready configuration:

```yaml
# Production ESP32 Audio Server Configuration
# Location: /etc/esp32-audio/config.yaml

server:
  # Bind to all interfaces
  host: 0.0.0.0

  # Standard port
  port: 8888

  # Support 8 concurrent streams
  max_workers: 8

recording:
  # Use default timestamped filenames
  output_pattern: ""

  # Enable MP3 conversion to save storage
  mp3_enabled: true

  # Don't keep raw files (saves ~87% space)
  keep_raw: false

  # Continuous mode (don't exit)
  single_connection: false

mqtt:
  # Internal MQTT broker
  broker: mqtt.internal.local
  port: 1883

  # Service account credentials
  username: esp32_audio_service
  password: "${MQTT_PASSWORD}"  # Set via environment variable
```

Run with:
```bash
export MQTT_PASSWORD="secret"
python tcp_server.py --config /etc/esp32-audio/config.yaml \
  --mqtt-password "${MQTT_PASSWORD}"
```
