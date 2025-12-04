# MQTT to Obsidian Bridge

Automatically append audio transcriptions from MQTT to Obsidian daily notes.

## Quick Start

```bash
# Create configuration
cp bridge_config.example.yaml bridge_config.yaml
# Edit bridge_config.yaml with your vault path and MQTT settings

# Run the bridge using the launcher script
./run_bridge.sh

# Or run directly with Python
python obsidian_mqtt_bridge.py --config bridge_config.yaml
```

**Note:** The launcher script (`run_bridge.sh`) automatically uses the virtual environment from the parent directory.

## What's Inside

- **obsidian_mqtt_bridge.py** - Main bridge script
- **bridge_config.example.yaml** - Example configuration file
- **OBSIDIAN_BRIDGE.md** - Complete documentation
- **test_bridge.sh** - Test script to verify functionality

## Documentation

See [OBSIDIAN_BRIDGE.md](OBSIDIAN_BRIDGE.md) for complete documentation including:
- Setup instructions
- Configuration options
- Integration with TCP server
- Troubleshooting
- Advanced usage (systemd, Docker)

## How It Works

```
ESP32 → TCP Server → Whisper Transcription → MQTT
                                                ↓
                                   obsidian_mqtt_bridge.py
                                                ↓
                              Obsidian Daily Note
```

## Example Daily Note Output

```markdown
## 14:30:45 - Voice Note

This is the transcribed text from your audio recording.

*File: audio_20251203_143045.mp3 | Language: en*
```

## Testing

```bash
# Send a test MQTT message
./test_bridge.sh

# Check your Obsidian daily note for the test entry
```
