#!/usr/bin/env python3
"""
Simple TCP server for receiving audio streams from ESP32
Saves received audio to a file and optionally plays it back
"""

import socket
import sys
import time
from datetime import datetime
import argparse

def main():
    parser = argparse.ArgumentParser(description='TCP Audio Server')
    parser.add_argument('--host', default='0.0.0.0', help='Host to bind to (default: 0.0.0.0)')
    parser.add_argument('--port', type=int, default=8888, help='Port to listen on (default: 8888)')
    parser.add_argument('--output', help='Output file (default: audio_<timestamp>.raw)')
    args = parser.parse_args()

    # Generate default output filename with timestamp
    if not args.output:
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        args.output = f'audio_{timestamp}.raw'

    print(f'ESP32 Audio TCP Server')
    print(f'=====================')
    print(f'Listening on {args.host}:{args.port}')
    print(f'Output file: {args.output}')
    print()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind((args.host, args.port))
        server_socket.listen(1)

        print('Waiting for connection...')

        try:
            conn, addr = server_socket.accept()
        except KeyboardInterrupt:
            print('\nServer stopped')
            return

        with conn:
            print(f'\nConnected by {addr[0]}:{addr[1]}')
            print('Receiving audio data...')
            print()

            total_bytes = 0
            start_time = time.time()

            try:
                with open(args.output, 'wb') as f:
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

                        print(f'\rReceived: {total_bytes:,} bytes | '
                              f'Duration: {duration:.1f}s | '
                              f'Rate: {rate/1024:.1f} KB/s', end='', flush=True)

                print()
                print()
                print('Connection closed')

            except KeyboardInterrupt:
                print()
                print('Interrupted by user')

            elapsed = time.time() - start_time
            duration = total_bytes / (48000 * 2)

            print(f'Total received: {total_bytes:,} bytes ({total_bytes/1024/1024:.2f} MB)')
            print(f'Audio duration: {duration:.1f} seconds')
            print(f'Transfer time: {elapsed:.1f} seconds')
            print(f'Average rate: {total_bytes/elapsed/1024:.1f} KB/s')
            print()
            print(f'Audio saved to: {args.output}')
            print()
            print('To convert to WAV:')
            print(f'  ffmpeg -f s16le -ar 48000 -ac 1 -i {args.output} output.wav')
            print(f'  sox -r 48000 -e signed -b 16 -c 1 {args.output} output.wav')
            print()
            print('To play directly:')
            print(f'  ffplay -f s16le -ar 48000 -ac 1 {args.output}')
            print(f'  aplay -f S16_LE -r 48000 -c 1 {args.output}')

if __name__ == '__main__':
    main()
