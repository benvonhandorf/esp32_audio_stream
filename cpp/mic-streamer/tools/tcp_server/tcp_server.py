#!/usr/bin/env python3
"""
Simple TCP server for receiving audio streams from ESP32
Saves received audio to files and encodes to MP3
Handles multiple connections concurrently using ThreadPoolExecutor
"""

import socket
import sys
import time
import os
from datetime import datetime
import argparse
from concurrent.futures import ThreadPoolExecutor
import threading
from openai import OpenAI
import io
import json
import yaml

try:
    from pydub import AudioSegment
    PYDUB_AVAILABLE = True
except ImportError:
    PYDUB_AVAILABLE = False

try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False

# Global connection counter with thread safety
connection_lock = threading.Lock()
connection_count = 0

# Global MQTT client (shared across threads)
mqtt_client = None
mqtt_enabled = False

TRANSCRIPTION_TIMEOUT_S = 120
SPEACHES_BASE_URL = 'http://primemover.local:8000/v1/'

client = OpenAI(api_key='NO_KEY', base_url=SPEACHES_BASE_URL, timeout=TRANSCRIPTION_TIMEOUT_S)
TRANSCRIPTION_MODEL_NAME = 'Systran/faster-whisper-large-v3'

def get_next_connection_number():
    """Thread-safe connection counter"""
    global connection_count
    with connection_lock:
        connection_count += 1
        return connection_count

def mqtt_on_connect(client, userdata, flags, reason_code, properties):
    """MQTT connection callback (API v2)"""
    if reason_code == 0:
        print(f"[MQTT] Connected successfully to broker")
    else:
        print(f"[MQTT] Connection failed with code {reason_code}")

def mqtt_on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    """MQTT disconnection callback (API v2)"""
    if reason_code != 0:
        print(f"[MQTT] Unexpected disconnection (code {reason_code})")

def mqtt_on_publish(client, userdata, mid, reason_code, properties):
    """MQTT publish callback (API v2)"""
    print(f"[MQTT] Message {mid} published successfully")

def setup_mqtt(broker, port=1883, username=None, password=None):
    """Initialize MQTT client and connect to broker"""
    global mqtt_client, mqtt_enabled

    if not MQTT_AVAILABLE:
        print("Warning: paho-mqtt not installed, MQTT publishing will be disabled")
        print("Install with: pip install paho-mqtt")
        mqtt_enabled = False
        return False

    try:
        # Use CallbackAPIVersion.VERSION2 for the modern callback API
        mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        mqtt_client.on_connect = mqtt_on_connect
        mqtt_client.on_disconnect = mqtt_on_disconnect
        mqtt_client.on_publish = mqtt_on_publish

        if username and password:
            mqtt_client.username_pw_set(username, password)

        print(f"[MQTT] Connecting to broker at {broker}:{port}...")
        mqtt_client.connect(broker, port, keepalive=60)
        mqtt_client.loop_start()  # Start background thread for MQTT

        mqtt_enabled = True
        return True

    except Exception as e:
        print(f"[MQTT] Failed to connect to broker: {e}")
        mqtt_enabled = False
        return False

def publish_transcription(text, metadata=None):
    """Publish transcription to MQTT topic"""
    global mqtt_client, mqtt_enabled

    if not mqtt_enabled or mqtt_client is None:
        return

    try:
        payload = {
            'text': text,
            'timestamp': datetime.now().isoformat()
        }

        if metadata:
            payload.update(metadata)

        json_payload = json.dumps(payload)
        result = mqtt_client.publish('sensors/transcription/text', json_payload, qos=1)

        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"[{threading.current_thread().name}] Published to MQTT: {text[:100]}...")
        else:
            print(f"[{threading.current_thread().name}] MQTT publish failed with code {result.rc}")

    except Exception as e:
        print(f"[{threading.current_thread().name}] Error publishing to MQTT: {e}")

def transcribe_file(mp3_file):

    with open(mp3_file, mode="rb") as input_data:

        transcription = client.audio.transcriptions.create(model=TRANSCRIPTION_MODEL_NAME,
                                                        file=input_data,
                                                        timestamp_granularities=['segment'],
                                                        response_format='verbose_json',
                                                        language='en',
                                                        timeout=TRANSCRIPTION_TIMEOUT_S)

        print(f"[{threading.current_thread().name}] Transcription complete: {transcription.text}")

        # Publish to MQTT if enabled
        metadata = {
            'file': os.path.basename(mp3_file),
            'language': transcription.language if hasattr(transcription, 'language') else 'en'
        }
        publish_transcription(transcription.text, metadata)


def convert_to_mp3(raw_file, mp3_file, sample_rate=48000, channels=1, sample_width=2):
    """Convert raw PCM audio to MP3 using pydub"""
    if not PYDUB_AVAILABLE:
        print(f"[{threading.current_thread().name}] Warning: pydub not available, skipping MP3 conversion")
        print("Install with: pip install pydub")
        print("Note: Also requires ffmpeg or libav to be installed")
        return False

    try:
        print(f"[{threading.current_thread().name}] Converting to MP3: {mp3_file}")

        # Load raw PCM data
        # sample_width: 2 = 16-bit
        audio = AudioSegment.from_raw(
            raw_file,
            sample_width=sample_width,
            frame_rate=sample_rate,
            channels=channels
        )

        # Export as MP3 with high quality settings
        audio.export(
            mp3_file,
            format="mp3",
            bitrate="192k",
            parameters=["-q:a", "0"]  # Highest quality
        )

        # Get file sizes for comparison
        raw_size = os.path.getsize(raw_file)
        mp3_size = os.path.getsize(mp3_file)
        compression_ratio = (1 - mp3_size / raw_size) * 100

        print(f"[{threading.current_thread().name}] MP3 conversion successful!")
        print(f"[{threading.current_thread().name}]   Raw size: {raw_size:,} bytes ({raw_size/1024/1024:.2f} MB)")
        print(f"[{threading.current_thread().name}]   MP3 size: {mp3_size:,} bytes ({mp3_size/1024/1024:.2f} MB)")
        print(f"[{threading.current_thread().name}]   Compression: {compression_ratio:.1f}% reduction")

        transcribe_file(mp3_file)

        return True

    except Exception as e:
        print(f"[{threading.current_thread().name}] Error converting to MP3: {e}")
        print("Note: pydub requires ffmpeg or libav to be installed")
        print("  Ubuntu/Debian: sudo apt-get install ffmpeg")
        print("  macOS: brew install ffmpeg")
        return False

def handle_client(conn, addr, output_file, convert_mp3=True, keep_raw=False):
    """Handle a single client connection (runs in thread pool)"""
    thread_name = threading.current_thread().name

    try:
        print(f'\n[{thread_name}] Connected by {addr[0]}:{addr[1]}')
        print(f'[{thread_name}] Receiving audio data to: {output_file}')

        total_bytes = 0
        start_time = time.time()

        try:
            with open(output_file, 'wb') as f:
                while True:
                    data = conn.recv(4096)
                    if not data:
                        break

                    f.write(data)
                    total_bytes += len(data)

                    # Print progress
                    elapsed = time.time() - start_time
                    rate = total_bytes / elapsed if elapsed > 0 else 0
                    duration = total_bytes / (48000 * 2)  # 48kHz, 16-bit = 2 bytes per sample

                    print(f'\r[{thread_name}] Received: {total_bytes:,} bytes | '
                          f'Duration: {duration:.1f}s | '
                          f'Rate: {rate/1024:.1f} KB/s', end='', flush=True)

            print()  # New line after progress
            print(f'[{thread_name}] Connection closed by client')

        except Exception as e:
            print(f'\n[{thread_name}] Error during receive: {e}')
            raise

        elapsed = time.time() - start_time
        duration = total_bytes / (48000 * 2)

        print(f'[{thread_name}] Total received: {total_bytes:,} bytes ({total_bytes/1024/1024:.2f} MB)')
        print(f'[{thread_name}] Audio duration: {duration:.1f} seconds')
        print(f'[{thread_name}] Transfer time: {elapsed:.1f} seconds')
        print(f'[{thread_name}] Average rate: {total_bytes/elapsed/1024:.1f} KB/s' if elapsed > 0 else 'N/A')
        print(f'[{thread_name}] Raw audio saved to: {output_file}')

        # Convert to MP3 if requested
        if convert_mp3 and total_bytes > 0:
            mp3_file = output_file.rsplit('.', 1)[0] + '.mp3'
            success = convert_to_mp3(output_file, mp3_file)

            if success:
                if not keep_raw:
                    print(f"[{thread_name}] Removing raw file: {output_file}")
                    os.remove(output_file)

                print(f'[{thread_name}] Final output: {mp3_file}')
            else:
                print(f'[{thread_name}] Error converting to mp3, kept raw file')

        print(f'[{thread_name}] Connection handler finished')
        print('-' * 80)

    except Exception as e:
        print(f'[{thread_name}] Exception in connection handler: {e}')
    finally:
        conn.close()

def accept_connections(server_socket, args, executor):
    """Accept connections and submit them to the thread pool"""
    # Create data directory if it doesn't exist
    data_dir = 'data'
    os.makedirs(data_dir, exist_ok=True)

    print('Ready to accept connections...')
    print(f'Saving files to: {os.path.abspath(data_dir)}/')

    try:
        while True:
            print('\nWaiting for connection...')

            try:
                conn, addr = server_socket.accept()
            except KeyboardInterrupt:
                print('\nServer stopped by user')
                break
            except Exception as e:
                print(f'Error accepting connection: {e}')
                continue

            conn_num = get_next_connection_number()

            # Generate output filename with timestamp and connection number
            if args.output:
                # User specified a pattern, add timestamp and counter
                base = os.path.basename(args.output).rsplit('.', 1)[0]
                ext = args.output.rsplit('.', 1)[1] if '.' in args.output else 'raw'
                timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
                output_file = os.path.join(data_dir, f'{base}_{timestamp}_{conn_num}.{ext}')
            else:
                # Default pattern
                timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
                output_file = os.path.join(data_dir, f'audio_{timestamp}_{conn_num}.raw')

            # Submit connection handling to thread pool
            future = executor.submit(
                handle_client,
                conn,
                addr,
                output_file,
                convert_mp3=not args.no_mp3,
                keep_raw=args.keep_raw
            )

            # Optionally add callback for completion
            def connection_done(fut, conn_num=conn_num):
                try:
                    fut.result()  # This will raise if there was an exception
                except Exception as e:
                    print(f'[Connection {conn_num}] Handler failed with exception: {e}')

            future.add_done_callback(connection_done)

            print(f'Connection {conn_num} submitted to thread pool')

            # Exit after first connection if --single flag is set
            if args.single:
                print('Single connection mode - waiting for completion then exiting')
                future.result()  # Wait for the connection to finish
                break

    except KeyboardInterrupt:
        print('\nServer stopped by user')
    finally:
        print('\nShutting down...')

def load_config(config_file):
    """Load configuration from YAML file"""
    try:
        with open(config_file, 'r') as f:
            config = yaml.safe_load(f)
            return config if config else {}
    except FileNotFoundError:
        print(f"Warning: Config file '{config_file}' not found, using defaults")
        return {}
    except yaml.YAMLError as e:
        print(f"Error parsing config file: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(
        description='TCP Audio Server with MP3 encoding and MQTT publishing',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Configuration file support:
  Use --config to specify a YAML configuration file. Command-line arguments
  override configuration file values. Example config.yaml:

    server:
      host: 0.0.0.0
      port: 8888
      max_workers: 4

    recording:
      output_pattern: audio.raw
      mp3_enabled: true
      keep_raw: false

    mqtt:
      broker: mqtt.example.com
      port: 1883
      username: user
      password: pass
        '''
    )
    parser.add_argument('--config', '-c', help='Path to YAML configuration file')
    parser.add_argument('--host', help='Host to bind to (default: 0.0.0.0)')
    parser.add_argument('--port', type=int, help='Port to listen on (default: 8888)')
    parser.add_argument('--output', help='Output file pattern (default: audio_<timestamp>.raw)')
    parser.add_argument('--single', action='store_true', help='Exit after first connection (default: continuous)')
    parser.add_argument('--no-mp3', action='store_true', help='Disable MP3 conversion, save as raw only')
    parser.add_argument('--keep-raw', action='store_true', help='Keep raw PCM file after MP3 conversion')
    parser.add_argument('--max-workers', type=int, help='Maximum concurrent connections (default: 4)')
    parser.add_argument('--mqtt-broker', help='MQTT broker hostname or IP address')
    parser.add_argument('--mqtt-port', type=int, help='MQTT broker port (default: 1883)')
    parser.add_argument('--mqtt-username', help='MQTT username (optional)')
    parser.add_argument('--mqtt-password', help='MQTT password (optional)')
    args = parser.parse_args()

    # Load configuration file if specified
    config = {}
    if args.config:
        config = load_config(args.config)

    # Merge config file with command-line arguments (CLI args take precedence)
    # Set defaults from config file, then override with CLI args
    server_config = config.get('server', {})
    recording_config = config.get('recording', {})
    mqtt_config = config.get('mqtt', {})

    # Apply configuration with priority: CLI args > config file > defaults
    args.host = args.host or server_config.get('host', '0.0.0.0')
    args.port = args.port or server_config.get('port', 8888)
    args.max_workers = args.max_workers or server_config.get('max_workers', 4)

    args.output = args.output or recording_config.get('output_pattern')
    if not args.no_mp3 and not recording_config.get('mp3_enabled', True):
        args.no_mp3 = True
    if not args.keep_raw and recording_config.get('keep_raw', False):
        args.keep_raw = True
    if not args.single and recording_config.get('single_connection', False):
        args.single = True

    args.mqtt_broker = args.mqtt_broker or mqtt_config.get('broker')
    args.mqtt_port = args.mqtt_port or mqtt_config.get('port', 1883)
    args.mqtt_username = args.mqtt_username or mqtt_config.get('username')
    args.mqtt_password = args.mqtt_password or mqtt_config.get('password')

    # Check for pydub if MP3 conversion is enabled
    if not args.no_mp3 and not PYDUB_AVAILABLE:
        print("Warning: pydub not installed, MP3 conversion will be disabled")
        print("Install with: pip install pydub")
        print()
        args.no_mp3 = True

    # Setup MQTT if broker is specified
    if args.mqtt_broker:
        setup_mqtt(
            args.mqtt_broker,
            args.mqtt_port,
            args.mqtt_username,
            args.mqtt_password
        )

    print(f'ESP32 Audio TCP Server')
    print(f'=====================')
    print(f'Listening on {args.host}:{args.port}')
    print(f'Mode: {"Single connection" if args.single else "Continuous (Ctrl+C to stop)"}')
    print(f'Max concurrent connections: {args.max_workers}')
    print(f'MP3 conversion: {"Disabled" if args.no_mp3 else "Enabled"}')
    if not args.no_mp3 and args.keep_raw:
        print(f'Keep raw files: Yes')
    if mqtt_enabled:
        print(f'MQTT publishing: Enabled (broker: {args.mqtt_broker}:{args.mqtt_port}, topic: sensors/transcription/text)')
    else:
        print(f'MQTT publishing: Disabled')
    print()

    # Create thread pool executor
    with ThreadPoolExecutor(max_workers=args.max_workers, thread_name_prefix='ClientHandler') as executor:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
            server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_socket.bind((args.host, args.port))
            server_socket.listen(5)  # Increased backlog for concurrent connections

            accept_connections(server_socket, args, executor)

        print('Waiting for all connection handlers to complete...')

    # Cleanup MQTT connection
    if mqtt_enabled and mqtt_client:
        print('Disconnecting from MQTT broker...')
        mqtt_client.loop_stop()
        mqtt_client.disconnect()

    print('All connections closed. Server shutdown complete.')

if __name__ == '__main__':
    main()
