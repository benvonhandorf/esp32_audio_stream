#!/bin/bash
# Test script for Obsidian MQTT Bridge
# This script tests the bridge by publishing a test message to MQTT

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}Obsidian MQTT Bridge Test${NC}"
echo "=========================="
echo

# Check if mosquitto_pub is available
if ! command -v mosquitto_pub &> /dev/null; then
    echo -e "${RED}Error: mosquitto_pub not found${NC}"
    echo "Install mosquitto clients:"
    echo "  Ubuntu/Debian: sudo apt install mosquitto-clients"
    echo "  macOS: brew install mosquitto"
    exit 1
fi

# Configuration
MQTT_BROKER="${MQTT_BROKER:-localhost}"
MQTT_PORT="${MQTT_PORT:-1883}"
MQTT_TOPIC="${MQTT_TOPIC:-sensors/transcription/text}"

echo -e "${YELLOW}Configuration:${NC}"
echo "  MQTT Broker: $MQTT_BROKER:$MQTT_PORT"
echo "  MQTT Topic: $MQTT_TOPIC"
echo

# Create test message
TIMESTAMP=$(date -Iseconds)
TEST_MESSAGE=$(cat <<EOF
{
  "text": "This is a test transcription from the Obsidian MQTT Bridge test script. The current time is $(date '+%H:%M:%S'). If you see this in your daily note, the bridge is working correctly!",
  "timestamp": "$TIMESTAMP",
  "file": "test_message.mp3",
  "language": "en"
}
EOF
)

echo -e "${YELLOW}Test Message:${NC}"
echo "$TEST_MESSAGE" | jq '.' 2>/dev/null || echo "$TEST_MESSAGE"
echo

# Publish message
echo -e "${YELLOW}Publishing to MQTT...${NC}"
if mosquitto_pub -h "$MQTT_BROKER" -p "$MQTT_PORT" -t "$MQTT_TOPIC" -m "$TEST_MESSAGE"; then
    echo -e "${GREEN}✓ Message published successfully${NC}"
    echo
    echo "Check your Obsidian daily note for today's date."
    echo "The note should contain a new entry with the timestamp and test message."
else
    echo -e "${RED}✗ Failed to publish message${NC}"
    echo "Check that:"
    echo "  1. MQTT broker is running (mosquitto)"
    echo "  2. Broker is accessible at $MQTT_BROKER:$MQTT_PORT"
    echo "  3. No firewall blocking the connection"
    exit 1
fi
