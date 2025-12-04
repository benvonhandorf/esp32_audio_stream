# Use Python 3.11 slim image as base
FROM python:3.11-slim

# Install system dependencies for audio processing
RUN apt-get update && apt-get install -y \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy requirements first for better layer caching
COPY requirements.txt .

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Copy the TCP server script and example config
COPY tcp_server.py .
COPY config.example.yaml .

# Create directory for audio files (server will create 'data' subdirectory)
RUN mkdir -p /app/data

# Expose the TCP server port
EXPOSE 8888

# Set volumes for data files and configuration
VOLUME ["/app/data"]
VOLUME ["/app/config"]

# Run the TCP server
# Default configuration: listen on all interfaces, port 8888
# Users can override with config file: -v $(pwd)/config.yaml:/app/config/config.yaml --config /app/config/config.yaml
# Or with command-line arguments
ENTRYPOINT ["python", "tcp_server.py"]
CMD ["--host", "0.0.0.0", "--port", "8888"]
