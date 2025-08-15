/*
 * NetTuner - An ESP32-based internet radio player
 * Copyright (C) 2025 Costin Stroie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <SSD1306Wire.h>
#include <ESPAsyncWebServer.h>

/**
 * @brief WiFi network credentials
 * These should be updated with your actual network credentials
 */
// WiFi credentials will be stored in SPIFFS
char ssid[64] = "";
char password[64] = "";

/**
 * @brief Web server instance running on port 80
 */
WebServer server(80);

/**
 * @brief Audio processing components
 * These pointers manage the audio streaming pipeline
 */
AudioGeneratorMP3 *mp3 = nullptr;           ///< MP3 decoder instance
AudioFileSourceHTTPStream *file = nullptr;  ///< HTTP stream source
AudioFileSourceBuffer *buff = nullptr;      ///< Buffer for audio data
AudioOutputI2S *out = nullptr;              ///< I2S audio output

/**
 * @brief Player state variables
 * Track current playback status, stream information, and volume level
 */
char currentStream[256] = "";      ///< URL of currently playing stream
char currentStreamName[128] = "";  ///< Name of currently playing stream
bool isPlaying = false;            ///< Playback status flag
int volume = 50;                   ///< Volume level (0-100)

/**
 * @brief I2S pin configuration for audio output
 * Defines the pin mapping for I2S audio interface
 */
#define I2S_DOUT      22  ///< I2S Data Out pin
#define I2S_BCLK      25  ///< I2S Bit Clock pin
#define I2S_LRC       26  ///< I2S Left/Right Clock pin
#define I2S_SD        21  ///< I2S Slave Data pin (amplifier control)

/**
 * @brief OLED display instance
 * SSD1306 OLED display connected via I2C (address 0x3c, SDA=5, SCL=4)
 */
SSD1306Wire display(0x3c, 5, 4); // ADDRESS, SDA, SCL

/**
 * @brief Rotary encoder pin configuration
 * Defines the pin mapping for the rotary encoder with push button
 */
#define ROTARY_CLK 18  ///< Rotary encoder clock pin (quadrature signal A)
#define ROTARY_DT 19   ///< Rotary encoder data pin (quadrature signal B)
#define ROTARY_SW 23   ///< Rotary encoder switch pin (push button)

/**
 * @brief Rotary encoder state variables
 * Track position, direction, timing, and button state for the rotary encoder
 */
class RotaryEncoder {
private:
  volatile int position = 0;
  int lastCLK = 0;
  volatile unsigned long lastRotaryTime = 0;
  bool buttonPressedFlag = false;

public:
  /**
   * @brief Handle rotary encoder rotation
   * This replaces the previous ISR with a cleaner approach
   */
  void handleRotation() {
    unsigned long currentTime = millis();
    
    // Debounce rotary encoder (ignore if less than 2ms since last event)
    if (currentTime - lastRotaryTime < 2) {
      return;
    }
    
    int CLK = digitalRead(ROTARY_CLK);  // Read clock signal
    int DT = digitalRead(ROTARY_DT);    // Read data signal
    
    // Only process when CLK transitions
    if (CLK != lastCLK) {
      // Determine rotation direction based on CLK and DT relationship
      if (CLK == 1 && DT == 0) {
        position++;      // Clockwise rotation
      } else if (CLK == 0 && DT == 1) {
        position++;      // Clockwise rotation (alternative pattern)
      } else if (CLK == 1 && DT == 1) {
        position--;      // Counter-clockwise rotation
      } else if (CLK == 0 && DT == 0) {
        position--;      // Counter-clockwise rotation (alternative pattern)
      }
      lastRotaryTime = currentTime;  // Update last event time
    }
    lastCLK = CLK;
  }
  
  /**
   * @brief Handle button press
   */
  void handleButtonPress() {
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    
    // Debounce the button press (ignore if less than 50ms since last press)
    if (interruptTime - lastInterruptTime > 50) {
      buttonPressedFlag = true;
    }
    lastInterruptTime = interruptTime;
  }
  
  /**
   * @brief Get current position
   */
  int getPosition() const {
    return position;
  }
  
  /**
   * @brief Set position
   */
  void setPosition(int pos) {
    position = pos;
  }
  
  /**
   * @brief Check if button was pressed
   */
  bool wasButtonPressed() {
    bool result = buttonPressedFlag;
    buttonPressedFlag = false;
    return result;
  }
};

RotaryEncoder rotaryEncoder;  ///< Global rotary encoder instance

/**
 * @brief Playlist data structure
 * Stores information about available audio streams
 */
struct StreamInfo {
  char name[128];  ///< Human-readable name of the stream
  char url[256];   ///< URL of the audio stream
};

/**
 * @brief Playlist storage and management variables
 * Array to store playlist entries and tracking variables
 */
StreamInfo playlist[20];    ///< Array of stream information (max 20 entries)
int playlistCount = 0;      ///< Number of valid entries in the playlist
int currentSelection = 0;   ///< Currently selected item in the playlist

/**
 * @brief Task management handles
 * Used to manage and reference created FreeRTOS tasks
 */
TaskHandle_t audioTaskHandle = NULL;  ///< Handle for the audio processing task

// Function declarations
void audioTask(void *parameter);
void setupI2S();
void startStream(const char* url, const char* name);
void stopStream();
void loadPlaylist();
void savePlaylist();
void setupRotaryEncoder();
void rotaryISR();
void updateDisplay();
void handleRotary();

// Web server handlers
void handleRoot();
void handlePlaylistPage();
void handleGetStreams();
void handlePostStreams();
void handlePlay();
void handleStop();
void handleVolume();
void handleStatus();
void handleWiFiConfig();
void handleWiFiScan();
void handleWiFiSave();
void loadWiFiCredentials();
void saveWiFiCredentials();

/**
 * @brief Arduino setup function
 * Initializes all system components and starts the web server
 */
void setup() {
  Serial.begin(115200);
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  // Initialize OLED display
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.clear();
  display.drawString(0, 0, "Connecting WiFi...");
  display.display();
  
  // Load WiFi credentials
  loadWiFiCredentials();
  
  // Connect to WiFi
  if (strlen(ssid) > 0) {
    WiFi.begin(ssid, password);
    int wifiAttempts = 0;
    const int maxAttempts = 20;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < maxAttempts) {
      delay(500);
      Serial.print(".");
      wifiAttempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to WiFi");
      display.clear();
      display.drawString(0, 0, "WiFi Connected");
      display.drawString(0, 15, WiFi.localIP().toString());
      display.display();
    } else {
      Serial.println("Failed to connect to WiFi");
      display.clear();
      display.drawString(0, 0, "WiFi Failed");
      display.drawString(0, 15, "Configure WiFi");
      display.display();
    }
  } else {
    Serial.println("No WiFi configured");
    display.clear();
    display.drawString(0, 0, "No WiFi Config");
    display.drawString(0, 15, "Configure WiFi");
    display.display();
  }
  
  // Setup I2S
  setupI2S();
  
  // Setup rotary encoder
  setupRotaryEncoder();
  
  // Load playlist
  loadPlaylist();
  
  // Create audio task on core 0
  xTaskCreatePinnedToCore(
    audioTask,      // Function
    "AudioTask",    // Name
    10000,          // Stack size
    NULL,           // Parameter
    1,              // Priority
    &audioTaskHandle, // Task handle
    0               // Core 0
  );
  
  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/playlist.html", HTTP_GET, handlePlaylistPage);
  server.on("/api/streams", HTTP_GET, handleGetStreams);
  server.on("/api/streams", HTTP_POST, handlePostStreams);
  server.on("/api/play", HTTP_POST, handlePlay);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/volume", HTTP_POST, handleVolume);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/wifi", HTTP_GET, handleWiFiConfig);
  server.on("/api/wifiscan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifisave", HTTP_POST, handleWiFiSave);
  
  server.serveStatic("/", SPIFFS, "/")
    .setDefaultFile("index.html")
    .setCacheControl("max-age=3600"); // Cache for 1 hour
  
  server.begin();
  
  // Update display
  updateDisplay();
}

/**
 * @brief Arduino main loop function
 * Handles web server requests and rotary encoder input
 */
void loop() {
  server.handleClient();  // Process incoming web requests
  handleRotary();         // Process rotary encoder input
  delay(10);              // Small delay to prevent busy waiting
}

/**
 * @brief Handle WiFi configuration page
 * Serves the WiFi configuration page
 */
void handleWiFiConfig() {
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>NetTuner - WiFi Configuration</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; }
    input, select, button { width: 100%; padding: 10px; margin: 5px 0; }
    button { background-color: #4CAF50; color: white; border: none; cursor: pointer; }
    button:hover { background-color: #45a049; }
    .network { padding: 10px; margin: 5px 0; border: 1px solid #ccc; cursor: pointer; }
    .network:hover { background-color: #f0f0f0; }
  </style>
</head>
<body>
  <h1>NetTuner - WiFi Configuration</h1>
  <div id="networks">Scanning for networks...</div>
  <form id="wifiForm">
    <input type="text" id="ssid" placeholder="Enter SSID" required>
    <input type="password" id="password" placeholder="Enter Password">
    <button type="submit">Save Configuration</button>
  </form>
  <button onclick="scanNetworks()">Refresh Networks</button>
  <button onclick="window.location.href='/'">Back to Main</button>

  <script>
    function scanNetworks() {
      document.getElementById('networks').innerHTML = 'Scanning for networks...';
      fetch('/api/wifiscan')
        .then(response => response.json())
        .then(data => {
          let html = '<h2>Available Networks:</h2>';
          data.forEach(network => {
            html += `<div class="network" onclick="selectNetwork('${network.ssid}')">${network.ssid} (${network.rssi}dBm)</div>`;
          });
          document.getElementById('networks').innerHTML = html;
        })
        .catch(error => {
          document.getElementById('networks').innerHTML = 'Error scanning networks';
          console.error('Error:', error);
        });
    }

    function selectNetwork(ssid) {
      document.getElementById('ssid').value = ssid;
    }

    document.getElementById('wifiForm').addEventListener('submit', function(e) {
      e.preventDefault();
      const ssid = document.getElementById('ssid').value;
      const password = document.getElementById('password').value;
      
      fetch('/api/wifisave', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid, password: password })
      })
      .then(response => response.text())
      .then(data => {
        alert(data);
        if (data === 'WiFi configuration saved') {
          window.location.href = '/';
        }
      })
      .catch(error => {
        alert('Error saving configuration');
        console.error('Error:', error);
      });
    });

    // Scan networks on page load
    scanNetworks();
  </script>
</body>
</html>
)=====";
  server.send(200, "text/html", html);
}

/**
 * @brief Handle WiFi network scan
 * Returns a list of available WiFi networks as JSON
 */
void handleWiFiScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i));
    json += "}";
  }
  json += "]";
  
  server.send(200, "application/json", json);
}

/**
 * @brief Handle WiFi configuration save
 * Saves WiFi credentials to SPIFFS
 */
void handleWiFiSave() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing JSON data");
    return;
  }
  
  String json = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  
  if (!doc.containsKey("ssid")) {
    server.send(400, "text/plain", "Missing SSID");
    return;
  }
  
  strncpy(ssid, doc["ssid"], sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  
  if (doc.containsKey("password")) {
    strncpy(password, doc["password"], sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
  } else {
    password[0] = '\0';
  }
  
  saveWiFiCredentials();
  server.send(200, "text/plain", "WiFi configuration saved");
}

/**
 * @brief Load WiFi credentials from SPIFFS
 */
void loadWiFiCredentials() {
  if (!SPIFFS.exists("/wifi.json")) {
    Serial.println("WiFi config file not found");
    return;
  }
  
  File file = SPIFFS.open("/wifi.json", "r");
  if (!file) {
    Serial.println("Failed to open WiFi config file");
    return;
  }
  
  size_t size = file.size();
  if (size > 512) {
    Serial.println("WiFi config file too large");
    file.close();
    return;
  }
  
  std::unique_ptr<char[]> buf(new char[size + 1]);
  if (file.readBytes(buf.get(), size) != size) {
    Serial.println("Failed to read WiFi config file");
    file.close();
    return;
  }
  buf[size] = '\0';
  file.close();
  
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.println("Failed to parse WiFi config JSON");
    return;
  }
  
  if (doc.containsKey("ssid")) {
    strncpy(ssid, doc["ssid"], sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
  }
  
  if (doc.containsKey("password")) {
    strncpy(password, doc["password"], sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
  }
  
  Serial.println("Loaded WiFi credentials from SPIFFS");
}

/**
 * @brief Save WiFi credentials to SPIFFS
 */
void saveWiFiCredentials() {
  DynamicJsonDocument doc(512);
  doc["ssid"] = ssid;
  doc["password"] = password;
  
  File file = SPIFFS.open("/wifi.json", "w");
  if (!file) {
    Serial.println("Failed to open WiFi config file for writing");
    return;
  }
  
  serializeJson(doc, file);
  file.close();
  
  Serial.println("Saved WiFi credentials to SPIFFS");
}

/**
 * @brief Audio processing task function
 * Runs on core 0 to handle continuous audio decoding and playback
 * (ESP32 has 2 main cores + 1 ultra-low-power core)
 * @param parameter Unused parameter required by FreeRTOS task function signature
 */
void audioTask(void *parameter) {
  while (true) {
    // If MP3 decoder is running, process the next audio frame
    if (mp3 && mp3->isRunning()) {
      // Process one frame of audio data
      if (!mp3->loop()) {
        // If loop() returns false, the stream has ended
        mp3->stop();        // Stop the decoder
        isPlaying = false;  // Update playback status
        updateDisplay();    // Refresh the display
      }
    }
    delay(1);  // Small delay to prevent monopolizing CPU
  }
}

/**
 * @brief Initialize I2S audio interface
 * Configures I2S pins and enables the audio amplifier
 */
void setupI2S() {
  // Configure I2S pins
  pinMode(I2S_SD, OUTPUT);
  digitalWrite(I2S_SD, HIGH); // Enable amplifier
}

/**
 * @brief Start streaming an audio stream
 * Stops any currently playing stream and begins playing a new one
 * @param url URL of the audio stream to play
 * @param name Human-readable name of the stream
 */
void startStream(const char* url, const char* name) {
  stopStream();  // Stop any currently playing stream
  
  // Validate inputs
  if (!url || !name) {
    Serial.println("Error: NULL pointer passed to startStream");
    return;
  }
  
  if (strlen(url) == 0 || strlen(name) == 0) {
    Serial.println("Error: Empty URL or name passed to startStream");
    return;
  }
  
  strncpy(currentStream, url, sizeof(currentStream) - 1);
  currentStream[sizeof(currentStream) - 1] = '\0';
  strncpy(currentStreamName, name, sizeof(currentStreamName) - 1);
  currentStreamName[sizeof(currentStreamName) - 1] = '\0';
  isPlaying = true;         // Set playback status to playing
  
  // Create new audio components for the stream
  file = new AudioFileSourceHTTPStream(url);  // HTTP stream source
  if (!file || !file->isOpen()) {
    Serial.println("Error: Failed to create HTTP stream source");
    isPlaying = false;
    updateDisplay();
    return;
  }
  
  buff = new AudioFileSourceBuffer(file, 2048); // Buffer with 2KB buffer size
  if (!buff) {
    Serial.println("Error: Failed to create buffer");
    delete file;
    file = nullptr;
    isPlaying = false;
    updateDisplay();
    return;
  }
  
  out = new AudioOutputI2S();                 // I2S output
  if (!out) {
    Serial.println("Error: Failed to create I2S output");
    delete buff;
    buff = nullptr;
    delete file;
    file = nullptr;
    isPlaying = false;
    updateDisplay();
    return;
  }
  
  mp3 = new AudioGeneratorMP3();              // MP3 decoder
  if (!mp3) {
    Serial.println("Error: Failed to create MP3 decoder");
    delete out;
    out = nullptr;
    delete buff;
    buff = nullptr;
    delete file;
    file = nullptr;
    isPlaying = false;
    updateDisplay();
    return;
  }
  
  // Set volume (0.0 to 1.0)
  out->SetGain(volume / 100.0);
  
  // Start the decoding process
  if (!mp3->begin(buff, out)) {
    Serial.println("Error: Failed to start MP3 decoder");
    delete mp3;
    mp3 = nullptr;
    delete out;
    out = nullptr;
    delete buff;
    buff = nullptr;
    delete file;
    file = nullptr;
    isPlaying = false;
    updateDisplay();
    return;
  }
  
  updateDisplay();  // Refresh the display with new playback info
}

/**
 * @brief Stop the currently playing stream
 * Cleans up audio components and resets playback state
 */
void stopStream() {
  // Stop and delete the MP3 decoder if it exists
  if (mp3) {
    if (mp3->isRunning()) {
      mp3->stop();  // Stop decoding
    }
    delete mp3;
    mp3 = nullptr;
  }
  
  // Delete the buffer if it exists
  if (buff) {
    delete buff;
    buff = nullptr;
  }
  
  // Delete the I2S output if it exists
  if (out) {
    delete out;
    out = nullptr;
  }
  
  // Delete the HTTP stream source if it exists
  if (file) {
    delete file;
    file = nullptr;
  }
  
  isPlaying = false;        // Set playback status to stopped
  currentStream[0] = '\0';       // Clear current stream URL
  currentStreamName[0] = '\0';   // Clear current stream name
  
  updateDisplay();  // Refresh the display
}

/**
 * @brief Load playlist from SPIFFS storage
 * Reads playlist.json from SPIFFS and populates the playlist array
 */
void loadPlaylist() {
  playlistCount = 0;  // Reset playlist count
  
  // If playlist file doesn't exist, create a default empty one
  if (!SPIFFS.exists("/playlist.json")) {
    // Create default playlist
    File file = SPIFFS.open("/playlist.json", "w");
    if (file) {
      file.print("[]");  // Empty JSON array
      file.close();
      Serial.println("Created default playlist file");
    } else {
      Serial.println("Error: Failed to create default playlist file");
    }
    return;
  }
  
  // Open the playlist file for reading
  File file = SPIFFS.open("/playlist.json", "r");
  if (!file) {
    Serial.println("Error: Failed to open playlist file for reading");
    return;  // Return if file couldn't be opened
  }
  
  // Check file size
  size_t size = file.size();
  if (size == 0) {
    Serial.println("Warning: Playlist file is empty");
    file.close();
    return;
  }
  
  if (size > 4096) {
    Serial.println("Error: Playlist file too large");
    file.close();
    return;
  }
  
  // Read the entire file into a buffer
  std::unique_ptr<char[]> buf(new char[size + 1]);
  if (file.readBytes(buf.get(), size) != size) {
    Serial.println("Error: Failed to read playlist file completely");
    file.close();
    return;
  }
  buf[size] = '\0'; // Null terminate
  file.close();
  
  // Parse the JSON data
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    Serial.println("Creating empty playlist");
    // Create default empty playlist
    File file = SPIFFS.open("/playlist.json", "w");
    if (file) {
      file.print("[]");  // Empty JSON array
      file.close();
      Serial.println("Created default playlist file");
    }
    return;
  }
  
  // Extract the array of streams
  JsonArray arr = doc.as<JsonArray>();
  playlistCount = 0;
  
  // Populate the playlist array with stream information
  for (JsonVariant value : arr) {
    if (playlistCount >= 20) {
      Serial.println("Warning: Playlist limit reached (20 entries)");
      break;  // Limit to 20 entries
    }
    
    // Validate required fields
    if (!value.containsKey("name") || !value.containsKey("url")) {
      Serial.println("Warning: Skipping playlist entry with missing fields");
      continue;
    }
    
    String name = value["name"].as<String>();
    String url = value["url"].as<String>();
    
    if (name.length() == 0 || url.length() == 0) {
      Serial.println("Warning: Skipping playlist entry with empty fields");
      continue;
    }
    
    strncpy(playlist[playlistCount].name, name.c_str(), sizeof(playlist[playlistCount].name) - 1);
    playlist[playlistCount].name[sizeof(playlist[playlistCount].name) - 1] = '\0';
    strncpy(playlist[playlistCount].url, url.c_str(), sizeof(playlist[playlistCount].url) - 1);
    playlist[playlistCount].url[sizeof(playlist[playlistCount].url) - 1] = '\0';
    playlistCount++;
  }
  
  Serial.print("Loaded ");
  Serial.print(playlistCount);
  Serial.println(" streams from playlist");
}

/**
 * @brief Save playlist to SPIFFS storage
 * Note: Playlist saving is handled by the web API, so this function is a placeholder
 */
void savePlaylist() {
  // Playlist saving is handled by the web API
}

/**
 * @brief Initialize rotary encoder hardware
 * Configures pins and attaches interrupt handlers for the rotary encoder
 */
void setupRotaryEncoder() {
  // Configure rotary encoder pins
  pinMode(ROTARY_CLK, INPUT);
  pinMode(ROTARY_DT, INPUT);
  pinMode(ROTARY_SW, INPUT_PULLUP);  // Enable internal pull-up resistor
  
  // Attach interrupt handler for rotary encoder rotation
  attachInterrupt(digitalPinToInterrupt(ROTARY_CLK), rotaryISR, CHANGE);
  
  // Attach interrupt handler for rotary encoder button press
  attachInterrupt(digitalPinToInterrupt(ROTARY_SW), []() {
    rotaryEncoder.handleButtonPress();
  }, FALLING);
}

/**
 * @brief Rotary encoder interrupt service routine
 * Called when the rotary encoder position changes
 * Kept minimal to reduce interrupt execution time
 */
void rotaryISR() {
  rotaryEncoder.handleRotation();
}

/**
 * @brief Handle rotary encoder input
 * Processes rotation and button press events from the rotary encoder
 */
void handleRotary() {
  static int lastRotaryPosition = 0;
  
  // Check if rotary encoder position has changed
  int currentPosition = rotaryEncoder.getPosition();
  if (currentPosition != lastRotaryPosition) {
    int diff = currentPosition - lastRotaryPosition;
    
    // Process clockwise rotation
    if (diff > 0) {
      // Rotate clockwise - volume up or next item
      if (isPlaying) {
        // If playing, increase volume by 5% (capped at 100%)
        volume = min(100, volume + 5);
        if (out) {
          out->SetGain(volume / 100.0);  // Update audio output gain
        }
      } else {
        // If not playing, select next item in playlist
        currentSelection = (currentSelection + 1) % max(1, playlistCount);
      }
    } else if (diff < 0) {
      // Process counter-clockwise rotation
      // Rotate counter-clockwise - volume down or previous item
      if (isPlaying) {
        // If playing, decrease volume by 5% (capped at 0%)
        volume = max(0, volume - 5);
        if (out) {
          out->SetGain(volume / 100.0);  // Update audio output gain
        }
      } else {
        // If not playing, select previous item in playlist
        currentSelection = (currentSelection - 1 + max(1, playlistCount)) % max(1, playlistCount);
      }
    }
    
    lastRotaryPosition = currentPosition;  // Update last position
    updateDisplay();                      // Refresh display with new values
  }
  
  // Process button press if detected
  if (rotaryEncoder.wasButtonPressed()) {
    // Only process if we have playlist items
    if (playlistCount > 0 && currentSelection < playlistCount) {
      if (isPlaying) {
        // If currently playing, stop playback
        stopStream();
      } else {
        // If not playing, start playback of selected stream
        startStream(playlist[currentSelection].url, playlist[currentSelection].name);
      }
    }
    updateDisplay();  // Refresh display
  }
}

/**
 * @brief Handle root page request
 * Serves the main index.html file
 */
void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

/**
 * @brief Handle playlist page request
 * Serves the playlist.html file
 */
void handlePlaylistPage() {
  File file = SPIFFS.open("/playlist.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

/**
 * @brief Handle GET request for streams
 * Returns the current playlist as JSON
 */
void handleGetStreams() {
  File file = SPIFFS.open("/playlist.json", "r");
  if (!file) {
    server.send(200, "application/json", "[]");
    return;
  }
  server.streamFile(file, "application/json");
  file.close();
}

/**
 * @brief Handle POST request for streams
 * Updates the playlist with new JSON data
 */
void handlePostStreams() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing JSON data");
    return;
  }
  
  String json = server.arg("plain");
  
  // Basic JSON validation
  if (json.length() == 0) {
    server.send(400, "text/plain", "Empty JSON data");
    return;
  }
  
  // Check if it looks like valid JSON
  json.trim();
  if (!(json.startsWith("[") && json.endsWith("]")) && 
      !(json.startsWith("{") && json.endsWith("}"))) {
    server.send(400, "text/plain", "Invalid JSON format");
    return;
  }
  
  File file = SPIFFS.open("/playlist.json", "w");
  if (!file) {
    server.send(500, "text/plain", "Failed to save playlist");
    return;
  }
  file.print(json);
  file.close();
  loadPlaylist(); // Reload playlist
  server.send(200, "text/plain", "OK");
}

/**
 * @brief Handle play request
 * Starts playing a stream with the given URL and name
 */
void handlePlay() {
  if (!server.hasArg("url") || !server.hasArg("name")) {
    server.send(400, "text/plain", "Missing required parameters: url and name");
    return;
  }
  
  String url = server.arg("url");
  String name = server.arg("name");
  
  if (url.length() == 0 || name.length() == 0) {
    server.send(400, "text/plain", "URL and name cannot be empty");
    return;
  }
  
  // Validate URL format
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    server.send(400, "text/plain", "Invalid URL format. Must start with http:// or https://");
    return;
  }
  
  startStream(url.c_str(), name.c_str());
  server.send(200, "text/plain", "OK");
}

/**
 * @brief Handle stop request
 * Stops the currently playing stream
 */
void handleStop() {
  stopStream();
  server.send(200, "text/plain", "OK");
}

/**
 * @brief Handle volume request
 * Sets the volume level
 */
void handleVolume() {
  if (!server.hasArg("volume")) {
    server.send(400, "text/plain", "Missing required parameter: volume");
    return;
  }
  
  String vol = server.arg("volume");
  int newVolume = vol.toInt();
  
  if (newVolume < 0 || newVolume > 100) {
    server.send(400, "text/plain", "Volume must be between 0 and 100");
    return;
  }
  
  volume = newVolume;
  if (out) {
    out->SetGain(volume / 100.0);
  }
  server.send(200, "text/plain", "OK");
}

/**
 * @brief Handle status request
 * Returns the current player status as JSON
 */
void handleStatus() {
  String status = "{";
  status += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
  status += "\"currentStream\":\"" + String(currentStream) + "\",";
  status += "\"currentStreamName\":\"" + String(currentStreamName) + "\",";
  status += "\"volume\":" + String(volume);
  status += "}";
  server.send(200, "application/json", status);
}

/**
 * @brief Update the OLED display with current status
 * Shows playback status, current stream, volume level, and playlist selection
 */
void updateDisplay() {
  display.clear();  // Clear the display
  
  if (isPlaying) {
    // Display when playing
    display.drawString(0, 0, "PLAYING");
    display.drawString(0, 15, currentStreamName);
    display.drawString(0, 30, "Volume: " + String(volume) + "%");
  } else {
    // Display when stopped
    display.drawString(0, 0, "STOPPED");
    if (playlistCount > 0 && currentSelection < playlistCount) {
      // Show selected playlist item
      display.drawString(0, 15, playlist[currentSelection].name);
      display.drawString(0, 30, "Press to play");
    } else {
      // Show message when no streams available
      display.drawString(0, 15, "No streams");
    }
    display.drawString(0, 45, "Vol: " + String(volume) + "%");
  }
  
  display.display();  // Send buffer to display
}
