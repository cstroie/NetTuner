# NetTuner

An ESP32-based internet radio player with web interface control

## Overview

NetTuner is an open-source internet radio player built on the ESP32 platform. It allows you to stream MP3 audio from HTTP URLs and control playback through a web interface or physical rotary encoder. The project features an OLED display for local status feedback and supports playlist management through a web API.

## Features

- **Internet Radio Streaming**: Play MP3 streams from HTTP URLs
- **Web Interface**: Control playback through a responsive web UI
- **Physical Controls**: Rotary encoder for volume control and navigation
- **OLED Display**: Real-time status information
- **Playlist Management**: Store and manage multiple radio stations
- **Volume Control**: Adjustable volume through web interface or rotary encoder
- **Battery Powered**: Designed to run on battery power

## Hardware Requirements

- ESP32 development board
- I2S DAC (e.g., MAX98357A) or amplifier
- SSD1306 128x64 OLED display
- Rotary encoder with push button
- Audio amplifier and speaker

### Pin Connections

| Component        | ESP32 Pin |
|------------------|-----------|
| I2S BCLK         | GPIO 25   |
| I2S LRC          | GPIO 26   |
| I2S DOUT         | GPIO 22   |
| I2S SD (Amplifier)| GPIO 21   |
| OLED SDA         | GPIO 5    |
| OLED SCL         | GPIO 4    |
| Rotary CLK       | GPIO 18   |
| Rotary DT        | GPIO 19   |
| Rotary SW        | GPIO 23   |

## Software Setup

1. Install PlatformIO
2. Clone this repository
3. Update WiFi credentials in `src/main.cpp`:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```
4. Build and upload the firmware:
   ```bash
   pio run -t upload
   pio run -t uploadfs
   ```

## Web Interface

Once connected to WiFi, access the web interface by navigating to the ESP32's IP address in a web browser.

### Main Controls
- **Play/Pause**: Start or stop playback of the selected stream
- **Volume Control**: Adjust volume through slider
- **Playlist Management**: Add, remove, and organize radio stations

### API Endpoints

| Endpoint         | Method | Description                  |
|------------------|--------|------------------------------|
| `/api/streams`   | GET    | Get all streams in playlist  |
| `/api/streams`   | POST   | Update playlist              |
| `/api/play`      | POST   | Start playing a stream       |
| `/api/stop`      | POST   | Stop playback                |
| `/api/volume`    | POST   | Set volume level             |
| `/api/status`    | GET    | Get current player status    |

## File Structure

```
├── data/              # Web interface files
│   ├── index.html     # Main control interface
│   └── playlist.html  # Playlist management
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
- SSD1306 library by ThingPulse
