#!/bin/bash
# Launcher script for Obsidian MQTT Bridge
# Uses the virtual environment from parent directory

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR=$SCRIPT_DIR
VENV_PYTHON="$PARENT_DIR/env/bin/python"

# Check if virtual environment exists
if [ ! -f "$VENV_PYTHON" ]; then
    echo "Error: Virtual environment not found at $PARENT_DIR/env/"
    echo "Please create it first:"
    echo "  cd $PARENT_DIR"
    echo "  python3 -m venv env"
    echo "  ./env/bin/pip install -r requirements.txt"
    exit 1
fi

# Check if config file exists
CONFIG_FILE="$SCRIPT_DIR/bridge_config.yaml"
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file not found: $CONFIG_FILE"
    echo "Please create it from the example:"
    echo "  cp bridge_config.example.yaml bridge_config.yaml"
    echo "  # Edit bridge_config.yaml with your vault path and MQTT settings"
    exit 1
fi

# Run the bridge
echo "Starting Obsidian MQTT Bridge..."
echo "Configuration: $CONFIG_FILE"
echo "Press Ctrl+C to stop"
echo ""

"$VENV_PYTHON" "$SCRIPT_DIR/obsidian_mqtt_bridge.py" --config "$CONFIG_FILE" "$@"
