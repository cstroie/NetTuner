# NetTuner

An ESP32-based internet radio player with web interface control

![NetTuner](https://img.shields.io/badge/status-active-brightgreen)
![PlatformIO](https://img.shields.io/badge/platformio-latest-blue)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)

## Overview

NetTuner is an open-source internet radio player built on the ESP32 platform. It allows you to stream MP3 audio from HTTP URLs and control playback through a web interface or physical rotary encoder. The project features an OLED display for local status feedback and supports playlist management through a web API.

## Features

- **Internet Radio Streaming**: Play MP3 streams from HTTP URLs
- **Web Interface**: Control playback through a responsive web UI
- **Physical Controls**: Rotary encoder for volume control and navigation
- **OLED Display**: Real-time status information with scrolling text
- **Playlist Management**: Store and manage multiple radio stations with JSON/M3U support
- **Volume Control**: Adjustable volume through web interface or rotary encoder
- **WiFi Configuration**: Web-based WiFi setup with network scanning and multiple network support
- **File Management**: Upload/download playlists in JSON or M3U formats
- **WebSocket Communication**: Real-time status updates between device and web interface
- **MPD Protocol Support**: Control via MPD clients

## Hardware Requirements

- ESP32 development board
- I2S DAC (e.g., MAX98357A) or amplifier
- SSD1306 128x64 OLED display
- Rotary encoder with push button
- Audio amplifier and speaker

### Pin Connections

| Component         | ESP32 Pin |
|-------------------|-----------|
| I2S BCLK          | GPIO 27   |
| I2S LRC           | GPIO 25   |
| I2S DOUT          | GPIO 26   |
| OLED SDA          | GPIO 5    |
| OLED SCL          | GPIO 4    |
| Rotary CLK        | GPIO 18   |
| Rotary DT         | GPIO 19   |
| Rotary SW         | GPIO 23   |

> **Note**: Pin assignments can be modified in `src/main.cpp` to match your specific hardware setup.

## Software Setup

1. Install [PlatformIO](https://platformio.org/)
2. Clone this repository:
   ```bash
   git clone https://github.com/yourusername/nettuner.git
   cd nettuner
   ```
3. Build and upload the firmware:
   ```bash
   pio run -t upload
   pio run -t uploadfs
   ```
4. After the device boots, connect to its WiFi access point (default: "NetTuner-Setup") or access the device's IP address on your network
5. Configure your WiFi networks through the web interface

## Web Interface

Once connected to WiFi, access the web interface by navigating to the ESP32's IP address in a web browser.

### Main Controls
- **Play/Pause**: Start or stop playback of the selected stream
- **Volume Control**: Adjust volume through slider or rotary encoder
- **Playlist Management**: Add, remove, and organize radio stations
- **WiFi Configuration**: Configure multiple WiFi networks with priority ordering

### Playlist Management
- Upload/download playlists in JSON or M3U formats
- Convert between JSON and M3U formats on-the-fly
- Manage individual streams through the web interface
- Real-time validation of stream URLs and names

### WiFi Configuration
- Scan for available networks
- Configure multiple WiFi networks with priority
- Automatic fallback to next available network
- Secure password storage

### API Endpoints

| Endpoint         | Method | Description                  |
|------------------|--------|------------------------------|
| `/`              | GET    | Main control interface       |
| `/playlist.html` | GET    | Playlist management          |
| `/wifi.html`     | GET    | WiFi configuration           |
| `/api/streams`   | GET    | Get all streams in playlist  |
| `/api/streams`   | POST   | Update playlist              |
| `/api/play`      | POST   | Start playing a stream       |
| `/api/stop`      | POST   | Stop playback                |
| `/api/volume`    | POST   | Set volume level             |
| `/api/status`    | GET    | Get current player status    |
| `/api/wifiscan`  | GET    | Scan for WiFi networks       |
| `/api/wificonfig`| GET    | Get current WiFi configuration|
| `/api/wifisave`  | POST   | Save WiFi configuration      |

> **Note**: WebSocket server runs on port 81 for real-time status updates

## File Structure

```
├── data/              # Web interface files
│   ├── index.html     # Main control interface
│   ├── playlist.html  # Playlist management
│   ├── wifi.html      # WiFi configuration
│   ├── styles.css     # Shared styles
│   └── scripts.js     # Shared JavaScript
├── src/
│   └── main.cpp       # Main firmware code
├── platformio.ini     # PlatformIO configuration
└── README.md          # This file
```

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Acknowledgments

- ESP32 Audio library by Earle F. Philhower
- ArduinoJson library by Benoit Blanchon
- SSD1306 library by Adafruit
- WebSocket library by Links2004
