# Obsidian MQTT Bridge

This tool monitors MQTT messages from the transcription service and automatically appends transcriptions to your Obsidian daily notes with timestamps.

## Features

- **Real-time Monitoring**: Listens to MQTT transcription topic
- **Daily Notes Integration**: Appends to daily notes (YYYY-MM-DD.md format)
- **Timestamps**: Each transcription includes time of arrival
- **Two Methods**: Direct file writing or Obsidian URI
- **Metadata Support**: Includes file name and language from transcription

## Prerequisites

```bash
# Install required Python packages
pip install paho-mqtt PyYAML

# Optional: For Obsidian URI method
# Install the "Advanced URI" plugin in Obsidian:
# Settings > Community plugins > Browse > "Advanced URI"
```

## Quick Start

### Method 1: Direct File Writing (Recommended)

This method writes directly to your vault's markdown files - it's faster and more reliable.

```bash
# Run the bridge with direct file access
python obsidian_mqtt_bridge.py \
  --vault MyVault \
  --vault-path /home/user/Documents/MyVault \
  --mqtt-broker localhost
```

### Method 2: Obsidian URI (Requires Plugin)

This method uses Obsidian's URI scheme via the Advanced URI plugin.

```bash
# Run the bridge with Obsidian URI
python obsidian_mqtt_bridge.py \
  --vault MyVault \
  --mqtt-broker localhost
```

### Method 3: Configuration File

Create a `bridge_config.yaml` file:

```yaml
obsidian:
  vault_name: "MyVault"
  vault_path: "/home/user/Documents/MyVault"

mqtt:
  broker: "localhost"
  port: 1883
  topic: "sensors/transcription/text"
```

Then run:

```bash
python obsidian_mqtt_bridge.py --config bridge_config.yaml
```

## Configuration

### Command-Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--vault` | Obsidian vault name | (required) |
| `--vault-path` | Absolute path to vault directory | None |
| `--mqtt-broker` | MQTT broker hostname | localhost |
| `--mqtt-port` | MQTT broker port | 1883 |
| `--mqtt-topic` | MQTT topic to subscribe | sensors/transcription/text |
| `--mqtt-username` | MQTT username | None |
| `--mqtt-password` | MQTT password | None |
| `--config` | Path to YAML config file | None |

### Configuration File Format

See [bridge_config.example.yaml](bridge_config.example.yaml) for a complete example.

```yaml
obsidian:
  vault_name: "MyVault"
  vault_path: "/path/to/vault"  # Optional but recommended

mqtt:
  broker: "mqtt.example.com"
  port: 1883
  topic: "sensors/transcription/text"
  username: "user"    # Optional
  password: "pass"    # Optional
```

## How It Works

### Data Flow

```
ESP32 Audio → TCP Server → Whisper Transcription → MQTT Publish
                                                           ↓
                                        Obsidian MQTT Bridge
                                                           ↓
                                         Daily Note (2025-12-03.md)
```

### MQTT Message Format

The bridge expects JSON messages on the MQTT topic:

```json
{
  "text": "This is the transcribed text",
  "timestamp": "2025-12-03T14:30:45.123456",
  "file": "audio_20251203_143045.mp3",
  "language": "en"
}
```

### Obsidian Daily Note Format

Transcriptions are appended to daily notes with this format:

```markdown
## 14:30:45 - Voice Note

This is the transcribed text

*File: audio_20251203_143045.mp3 | Language: en*
```

### Daily Note Creation

- Daily notes are named `YYYY-MM-DD.md` (e.g., `2025-12-03.md`)
- If a daily note doesn't exist, it's created automatically
- Notes are created in the vault root (or configured folder)

## Integration with TCP Server

The [tcp_server.py](tcp_server.py) already publishes transcriptions to MQTT. Just make sure:

1. MQTT is enabled in the TCP server configuration
2. Both services use the same MQTT broker and topic
3. The bridge is running before transcriptions arrive

### Example Setup

**Terminal 1 - TCP Server:**
```bash
python tcp_server.py --config config.yaml
```

**Terminal 2 - Obsidian Bridge:**
```bash
python obsidian_mqtt_bridge.py --config bridge_config.yaml
```

**Terminal 3 - MQTT Broker (if needed):**
```bash
# Install mosquitto
sudo apt install mosquitto mosquitto-clients  # Ubuntu/Debian
brew install mosquitto                         # macOS

# Start broker
mosquitto
```

## Troubleshooting

### Issue: "Connection failed"
- Check MQTT broker is running: `mosquitto_sub -h localhost -t '#' -v`
- Verify broker hostname and port
- Check firewall settings

### Issue: "Obsidian URI not working"
- Install the "Advanced URI" plugin in Obsidian
- Or use direct file writing with `--vault-path`

### Issue: "File not found" or "Permission denied"
- Check `--vault-path` is correct absolute path
- Ensure vault directory is readable/writable
- Try running with `ls -la /path/to/vault`

### Issue: "No transcriptions appearing"
- Verify TCP server is publishing to MQTT: `mosquitto_sub -h localhost -t sensors/transcription/text -v`
- Check topic name matches in both services
- Ensure transcription is working in TCP server

### Issue: "Wrong daily note file"
- The script uses the timestamp from the MQTT message
- Check your system timezone is correct
- Verify MQTT message timestamp format

## Advanced Usage

### Custom Daily Note Location

If you keep daily notes in a subdirectory (e.g., `Daily/`), modify the vault path:

```bash
python obsidian_mqtt_bridge.py \
  --vault MyVault \
  --vault-path /path/to/vault/Daily
```

### Running as a Service

Create a systemd service file `/etc/systemd/system/obsidian-bridge.service`:

```ini
[Unit]
Description=Obsidian MQTT Bridge
After=network.target mosquitto.service

[Service]
Type=simple
User=youruser
WorkingDirectory=/home/youruser/projects/esp32_audio_stream/cpp/mic-streamer/tools/tcp_server
ExecStart=/usr/bin/python3 obsidian_mqtt_bridge.py --config bridge_config.yaml
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Then:
```bash
sudo systemctl enable obsidian-bridge
sudo systemctl start obsidian-bridge
sudo systemctl status obsidian-bridge
```

### Docker Deployment

Create a `Dockerfile.bridge`:

```dockerfile
FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir paho-mqtt PyYAML

COPY obsidian_mqtt_bridge.py .
COPY bridge_config.yaml .

CMD ["python", "obsidian_mqtt_bridge.py", "--config", "bridge_config.yaml"]
```

Build and run:
```bash
docker build -f Dockerfile.bridge -t obsidian-bridge .
docker run -v /path/to/vault:/vault obsidian-bridge
```

## Testing

### Test MQTT Connection

```bash
# Subscribe to transcription topic
mosquitto_sub -h localhost -t sensors/transcription/text -v
```

### Send Test Message

```bash
# Publish test transcription
mosquitto_pub -h localhost -t sensors/transcription/text \
  -m '{"text":"This is a test transcription","timestamp":"2025-12-03T14:30:00"}'
```

### Verify Daily Note

Check your vault for today's daily note (e.g., `2025-12-03.md`) and verify the transcription appears.

## Performance

- **Latency**: < 1 second from MQTT message to file write
- **Resource Usage**: ~10-20 MB RAM, negligible CPU
- **Concurrent Handling**: Processes messages sequentially (single-threaded)

## Security Considerations

- **MQTT Authentication**: Use username/password for production
- **File Permissions**: Ensure vault directory has appropriate permissions
- **Network Security**: Use MQTT over TLS for remote brokers
- **Secrets**: Don't commit `bridge_config.yaml` with credentials to git

## See Also

- [TCP Server Documentation](README.md)
- [TCP Server Configuration](CONFIG.md)
- [Obsidian Advanced URI Plugin](https://github.com/Vinzent03/obsidian-advanced-uri)
- [MQTT Protocol](https://mqtt.org/)
