#!/bin/bash
# Convert raw PCM audio files to WAV format

if [ $# -eq 0 ]; then
    echo "Usage: $0 <input.raw> [output.wav]"
    echo ""
    echo "Converts raw PCM audio (48kHz, 16-bit, mono) to WAV format"
    echo ""
    echo "Examples:"
    echo "  $0 audio_1234567890.raw"
    echo "  $0 audio_1234567890.raw my_audio.wav"
    exit 1
fi

INPUT="$1"
OUTPUT="${2:-${INPUT%.raw}.wav}"

if [ ! -f "$INPUT" ]; then
    echo "Error: Input file '$INPUT' not found"
    exit 1
fi

# Check if ffmpeg is available
if command -v ffmpeg &> /dev/null; then
    echo "Converting $INPUT to $OUTPUT using ffmpeg..."
    ffmpeg -f s16le -ar 48000 -ac 1 -i "$INPUT" "$OUTPUT"
    if [ $? -eq 0 ]; then
        echo "Success! Output: $OUTPUT"
        SIZE=$(du -h "$OUTPUT" | cut -f1)
        echo "File size: $SIZE"
    fi
# Check if sox is available
elif command -v sox &> /dev/null; then
    echo "Converting $INPUT to $OUTPUT using sox..."
    sox -r 48000 -e signed -b 16 -c 1 "$INPUT" "$OUTPUT"
    if [ $? -eq 0 ]; then
        echo "Success! Output: $OUTPUT"
        SIZE=$(du -h "$OUTPUT" | cut -f1)
        echo "File size: $SIZE"
    fi
else
    echo "Error: Neither ffmpeg nor sox found"
    echo "Please install one of them:"
    echo "  Ubuntu/Debian: sudo apt-get install ffmpeg"
    echo "  Ubuntu/Debian: sudo apt-get install sox"
    echo "  macOS: brew install ffmpeg"
    echo "  macOS: brew install sox"
    exit 1
fi
