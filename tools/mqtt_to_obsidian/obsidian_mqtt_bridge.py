#!/usr/bin/env python3
"""
Obsidian MQTT Bridge
Monitors MQTT transcription topic and appends transcriptions to Obsidian daily notes
Uses Obsidian URI scheme to open/create daily notes and append content
"""

import argparse
import json
import sys
import os
import subprocess
from datetime import datetime
from pathlib import Path
import urllib.parse

try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False
    print("Error: paho-mqtt is required")
    print("Install with: pip install paho-mqtt")
    sys.exit(1)

try:
    import yaml
    YAML_AVAILABLE = True
except ImportError:
    YAML_AVAILABLE = False


class ObsidianMQTTBridge:
    """Bridge between MQTT transcriptions and Obsidian daily notes"""

    def __init__(self, vault_name, vault_path=None, mqtt_broker='localhost',
                 mqtt_port=1883, mqtt_topic='sensors/transcription/text',
                 mqtt_username=None, mqtt_password=None):
        """
        Initialize the bridge

        Args:
            vault_name: Name of the Obsidian vault
            vault_path: Absolute path to vault directory (for direct file writing)
            mqtt_broker: MQTT broker hostname
            mqtt_port: MQTT broker port
            mqtt_topic: MQTT topic to subscribe to
            mqtt_username: MQTT username (optional)
            mqtt_password: MQTT password (optional)
        """
        self.vault_name = vault_name
        self.vault_path = Path(vault_path) if vault_path else None
        self.mqtt_broker = mqtt_broker
        self.mqtt_port = mqtt_port
        self.mqtt_topic = mqtt_topic
        self.mqtt_username = mqtt_username
        self.mqtt_password = mqtt_password

        # Create MQTT client
        self.client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_disconnect

        if mqtt_username and mqtt_password:
            self.client.username_pw_set(mqtt_username, mqtt_password)

    def on_connect(self, client, userdata, flags, reason_code, properties):
        """Callback when connected to MQTT broker"""
        if reason_code == 0:
            print(f"Connected to MQTT broker at {self.mqtt_broker}:{self.mqtt_port}")
            print(f"Subscribing to topic: {self.mqtt_topic}")
            client.subscribe(self.mqtt_topic)
        else:
            print(f"Connection failed with code {reason_code}")

    def on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties):
        """Callback when disconnected from MQTT broker"""
        if reason_code != 0:
            print(f"Unexpected disconnection (code {reason_code})")

    def on_message(self, client, userdata, msg):
        """Callback when message received on subscribed topic"""
        try:
            # Parse JSON payload
            payload = json.loads(msg.payload.decode())

            # Extract text and timestamp
            text = payload.get('text', '').strip()
            timestamp_str = payload.get('timestamp', '')

            if not text:
                print("Warning: Received empty transcription text")
                return

            # Parse timestamp or use current time
            if timestamp_str:
                try:
                    timestamp = datetime.fromisoformat(timestamp_str)
                except ValueError:
                    timestamp = datetime.now()
            else:
                timestamp = datetime.now()

            print(f"\n[{timestamp.strftime('%H:%M:%S')}] Received transcription:")
            print(f"  Text: {text[:100]}{'...' if len(text) > 100 else ''}")

            # Append to Obsidian daily note
            self.append_to_daily_note(text, timestamp, payload)

        except json.JSONDecodeError as e:
            print(f"Error parsing MQTT message: {e}")
            print(f"Raw payload: {msg.payload}")
        except Exception as e:
            print(f"Error processing message: {e}")

    def append_to_daily_note(self, text, timestamp, metadata=None):
        """
        Append transcription to Obsidian daily note

        Args:
            text: Transcription text to append
            timestamp: Datetime object for the transcription
            metadata: Additional metadata from MQTT message
        """
        # Format the entry
        time_str = timestamp.strftime('%H:%M:%S')

        # Build the content to append
        content = f"\n## {time_str} - Voice Note\n\n{text}\n"

        # Add metadata if available
        if metadata:
            file_name = metadata.get('file')
            language = metadata.get('language')
            if file_name or language:
                content += "\n*"
                if file_name:
                    content += f"File: {file_name}"
                if language:
                    content += f" | Language: {language}"
                content += "*\n"

        # Try direct file writing if vault path is available
        if self.vault_path:
            success = self.append_via_file(content, timestamp)
            if success:
                return

        # Fallback to Obsidian URI
        self.append_via_uri(content, timestamp)

    def append_via_file(self, content, timestamp):
        """
        Append to daily note by directly writing to file

        Args:
            content: Content to append
            timestamp: Datetime object for determining the daily note

        Returns:
            True if successful, False otherwise
        """
        try:
            # Determine daily note filename (YYYY-MM-DD.md)
            date_str = timestamp.strftime('%Y-%m-%d')
            daily_note_path = self.vault_path / f"{date_str}.md"

            # Create file if it doesn't exist
            if not daily_note_path.exists():
                with open(daily_note_path, 'w') as f:
                    f.write(f"# {date_str}\n\n")
                print(f"Created new daily note: {daily_note_path}")

            # Append content
            with open(daily_note_path, 'a') as f:
                f.write(content)

            print(f"Appended to daily note: {daily_note_path}")
            return True

        except Exception as e:
            print(f"Error writing to file: {e}")
            return False

    def append_via_uri(self, content, timestamp):
        """
        Append to daily note using Obsidian URI scheme

        Args:
            content: Content to append
            timestamp: Datetime object for determining the daily note
        """
        try:
            # Encode content for URI
            encoded_content = urllib.parse.quote(content)

            # Build Obsidian URI
            # obsidian://advanced-uri?vault=VaultName&daily=true&mode=append&data=content
            # obsidian://daily?mode=append&data=content
            uri = f"obsidian://daily?"
            uri += f"prepend=true"
            uri += f"&content={encoded_content}"

            # Open URI (this requires the Advanced URI plugin in Obsidian)
            if sys.platform == 'darwin':  # macOS
                subprocess.run(['open', uri], check=True)
            elif sys.platform == 'linux':
                subprocess.run(['xdg-open', uri], check=True)
            elif sys.platform == 'win32':
                subprocess.run(['start', uri], shell=True, check=True)

            print(f"Opened Obsidian URI for daily note")

        except Exception as e:
            print(f"Error opening Obsidian URI: {e}")

    def run(self):
        """Start the bridge and listen for MQTT messages"""
        print("Obsidian MQTT Bridge")
        print("=" * 50)
        print(f"Vault: {self.vault_name}")
        if self.vault_path:
            print(f"Vault Path: {self.vault_path}")
        print(f"MQTT Broker: {self.mqtt_broker}:{self.mqtt_port}")
        print(f"MQTT Topic: {self.mqtt_topic}")
        print()
        print("Waiting for transcriptions...")
        print("Press Ctrl+C to stop")
        print()

        try:
            # Connect to MQTT broker
            self.client.connect(self.mqtt_broker, self.mqtt_port, keepalive=60)

            # Start listening loop
            self.client.loop_forever()

        except KeyboardInterrupt:
            print("\nStopping bridge...")
        except Exception as e:
            print(f"Error: {e}")
        finally:
            self.client.disconnect()
            print("Disconnected from MQTT broker")


def load_config(config_file):
    """Load configuration from YAML file"""
    if not YAML_AVAILABLE:
        return {}

    try:
        with open(config_file, 'r') as f:
            config = yaml.safe_load(f)
            return config if config else {}
    except FileNotFoundError:
        print(f"Warning: Config file '{config_file}' not found, using command-line args")
        return {}
    except yaml.YAMLError as e:
        print(f"Error parsing config file: {e}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description='Bridge MQTT transcriptions to Obsidian daily notes',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Example usage:

  # Using direct file writing (recommended)
  python obsidian_mqtt_bridge.py --vault MyVault --vault-path /path/to/vault \\
      --mqtt-broker localhost

  # Using Obsidian URI (requires Advanced URI plugin)
  python obsidian_mqtt_bridge.py --vault MyVault --mqtt-broker mqtt.local

  # Using configuration file
  python obsidian_mqtt_bridge.py --config bridge_config.yaml

Configuration file format (bridge_config.yaml):

  obsidian:
    vault_name: MyVault
    vault_path: /Users/me/Documents/MyVault

  mqtt:
    broker: localhost
    port: 1883
    topic: sensors/transcription/text
    username: user
    password: pass

Notes:
  - Daily notes are named YYYY-MM-DD.md (e.g., 2025-12-03.md)
  - Transcriptions are appended with ## HH:MM:SS - Voice Note headers
        '''
    )

    parser.add_argument('--config', '-c', help='Path to YAML configuration file')
    parser.add_argument('--vault', help='Obsidian vault name (required)')
    parser.add_argument('--vault-path', help='Absolute path to vault directory (optional, enables direct file writing)')
    parser.add_argument('--mqtt-broker', help='MQTT broker hostname (default: localhost)')
    parser.add_argument('--mqtt-port', type=int, help='MQTT broker port (default: 1883)')
    parser.add_argument('--mqtt-topic', help='MQTT topic to subscribe to (default: sensors/transcription/text)')
    parser.add_argument('--mqtt-username', help='MQTT username (optional)')
    parser.add_argument('--mqtt-password', help='MQTT password (optional)')

    args = parser.parse_args()

    # Load configuration file if specified
    config = {}
    if args.config:
        config = load_config(args.config)

    # Merge config with command-line arguments (CLI takes precedence)
    obsidian_config = config.get('obsidian', {})
    mqtt_config = config.get('mqtt', {})

    vault_name = args.vault or obsidian_config.get('vault_name')
    vault_path = args.vault_path or obsidian_config.get('vault_path')
    mqtt_broker = args.mqtt_broker or mqtt_config.get('broker', 'localhost')
    mqtt_port = args.mqtt_port or mqtt_config.get('port', 1883)
    mqtt_topic = args.mqtt_topic or mqtt_config.get('topic', 'sensors/transcription/text')
    mqtt_username = args.mqtt_username or mqtt_config.get('username')
    mqtt_password = args.mqtt_password or mqtt_config.get('password')

    # Validate required arguments
    # if not vault_name:
    #     print("Error: --vault (vault name) is required")
    #     print("Run with --help for usage information")
    #     sys.exit(1)

    # # Validate vault path if provided
    # if vault_path:
    #     vault_path_obj = Path(vault_path)
    #     if not vault_path_obj.exists():
    #         print(f"Error: Vault path does not exist: {vault_path}")
    #         sys.exit(1)
    #     if not vault_path_obj.is_dir():
    #         print(f"Error: Vault path is not a directory: {vault_path}")
    #         sys.exit(1)

    # Create and run bridge
    bridge = ObsidianMQTTBridge(
        vault_name=vault_name,
        vault_path=vault_path,
        mqtt_broker=mqtt_broker,
        mqtt_port=mqtt_port,
        mqtt_topic=mqtt_topic,
        mqtt_username=mqtt_username,
        mqtt_password=mqtt_password
    )

    bridge.run()


if __name__ == '__main__':
    main()
