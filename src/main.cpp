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
#include "Audio.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <WebSocketsServer.h>

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
WebSocketsServer webSocket(81);

/**
 * @brief MPD server instance running on port 6600
 */
WiFiServer mpdServer(6600);
WiFiClient mpdClient;

/**
 * @brief Audio processing components
 * These pointers manage the audio streaming pipeline
 */
Audio *audio = nullptr;                     ///< Audio instance for ESP32-audioI2S


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
Adafruit_SSD1306 display(128, 64, &Wire, -1); // 128x64 display, using Wire, no reset pin

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
    
    // Debounce rotary encoder (ignore if less than 5ms since last event)
    if (currentTime - lastRotaryTime < 5) {
      return;
    }
    
    int CLK = digitalRead(ROTARY_CLK);  // Read clock signal
    int DT = digitalRead(ROTARY_DT);    // Read data signal
    
    // Only process when CLK transitions from LOW to HIGH
    if (CLK == HIGH && lastCLK == LOW) {
      // Determine rotation direction based on DT state
      if (DT == LOW) {
        position++;      // Clockwise rotation
      } else {
        position--;      // Counter-clockwise rotation
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
void setupAudioOutput();
void startStream(const char* url, const char* name);
void stopStream();
void loadPlaylist();
void savePlaylist();
void setupRotaryEncoder();
void rotaryISR();
void updateDisplay();
void handleRotary();
void sendStatusToClients();

// MPD functions
void handleMPDClient();
String mpdResponseOK();
String mpdResponseError(const String& message);
void handleMPDCommand(const String& command);

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
void handleConfigPage();
void handleGetConfig();
void handleSaveConfig();
void loadWiFiCredentials();
void saveWiFiCredentials();
void loadConfig();
void saveConfig();

// WebSocket handlers
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

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
  
  // Test SPIFFS write capability
  if (!SPIFFS.exists("/spiffs_test")) {
    Serial.println("Testing SPIFFS write capability...");
    File testFile = SPIFFS.open("/spiffs_test", "w");
    if (!testFile) {
      Serial.println("ERROR: Failed to create SPIFFS test file!");
    } else {
      if (testFile.println("SPIFFS write test - OK")) {
        Serial.println("SPIFFS write test successful");
      } else {
        Serial.println("ERROR: Failed to write to SPIFFS test file!");
      }
      testFile.close();
    }
  } else {
    Serial.println("SPIFFS write test file already exists - SPIFFS is working");
  }
  
  // Initialize OLED display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();
  
  // Load WiFi credentials
  loadWiFiCredentials();
  
  // Connect to WiFi
  if (strlen(ssid) > 0) {
    WiFi.setHostname("NetTuner");
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
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi Connected");
      display.println(WiFi.localIP().toString());
      display.display();
    } else {
      Serial.println("Failed to connect to WiFi");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi Failed");
      display.println("Configure WiFi");
      display.display();
    }
  } else {
    Serial.println("No WiFi configured");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("No WiFi Config");
    display.println("Configure WiFi");
    display.display();
    
    // Start WiFi access point mode
    WiFi.softAP("NetTuner-Setup");
    Serial.println("Access Point Started");
    display.println("AP: NetTuner-Setup");
    display.println("192.168.4.1");
    display.display();
  }
  
  // Setup audio output
  setupAudioOutput();
  
  // Load configuration
  loadConfig();
  
  // Setup rotary encoder
  setupRotaryEncoder();
  
  // Load playlist
  loadPlaylist();
  
  
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
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSaveConfig);
   
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/styles.css", SPIFFS, "/styles.css");
  server.serveStatic("/scripts.js", SPIFFS, "/scripts.js");
  
  // Start server on both AP and station interfaces if in AP mode
  server.begin();
  
  // Setup WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Start MPD server
  mpdServer.begin();
  
  // Update display
  updateDisplay();
}

/**
 * @brief Arduino main loop function
 * Handles web server requests, WebSocket events, rotary encoder input, and MPD commands
 */
void loop() {
  server.handleClient();   // Process incoming web requests
  webSocket.loop();        // Process WebSocket events
  handleRotary();          // Process rotary encoder input
  handleMPDClient();       // Process MPD commands
  
  // Process audio streaming
  if (audio) {
    audio->loop();
  }
  
  delay(10);               // Small delay to prevent busy waiting
}

/**
 * @brief Handle WiFi configuration page
 * Serves the WiFi configuration page
 */
void handleWiFiConfig() {
  File file = SPIFFS.open("/wifi.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

/**
 * @brief Handle configuration page
 * Serves the audio configuration page
 */
void handleConfigPage() {
  File file = SPIFFS.open("/config.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

/**
 * @brief Handle GET request for configuration
 * Returns the current configuration as JSON
 */
void handleGetConfig() {
  // No audio output configuration needed with ESP32-audioI2S
  String config = "{}";
  server.send(200, "application/json", config);
}

/**
 * @brief Handle POST request for configuration
 * Updates the configuration with new data
 */
void handleSaveConfig() {
  // No audio output configuration needed with ESP32-audioI2S
  server.send(200, "text/plain", "Configuration saved");
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
  
  // Validate SSID length
  String newSSID = doc["ssid"].as<String>();
  if (newSSID.length() == 0 || newSSID.length() >= sizeof(ssid)) {
    server.send(400, "text/plain", "Invalid SSID length");
    return;
  }
  
  strncpy(ssid, newSSID.c_str(), sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  
  if (doc.containsKey("password")) {
    String newPassword = doc["password"].as<String>();
    // Validate password length
    if (newPassword.length() >= sizeof(password)) {
      server.send(400, "text/plain", "Password too long");
      return;
    }
    strncpy(password, newPassword.c_str(), sizeof(password) - 1);
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
  
  if (size == 0) {
    Serial.println("WiFi config file is empty");
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
    Serial.print("Failed to parse WiFi config JSON: ");
    Serial.println(error.c_str());
    return;
  }
  
  if (doc.containsKey("ssid")) {
    strncpy(ssid, doc["ssid"], sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
  }
  
  if (doc.containsKey("password")) {
    const char* pwd = doc["password"];
    if (pwd) {
      strncpy(password, pwd, sizeof(password) - 1);
      password[sizeof(password) - 1] = '\0';
    } else {
      password[0] = '\0';
    }
  }
  
  Serial.println("Loaded WiFi credentials from SPIFFS");
  Serial.print("SSID: ");
  Serial.println(ssid);
  // Only print password if it exists and is not empty
  if (strlen(password) > 0) {
    Serial.println("Password: [REDACTED]");
  } else {
    Serial.println("Password: [NONE]");
  }
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
  
  size_t bytesWritten = serializeJson(doc, file);
  if (bytesWritten == 0) {
    Serial.println("Failed to write WiFi config to file");
    file.close();
    return;
  }
  file.close();
  
  Serial.println("Saved WiFi credentials to SPIFFS");
}


/**
 * @brief Initialize audio output interface
 * Configures the selected audio output method
 */
void setupAudioOutput() {
  // Configure amplifier control pin
  pinMode(I2S_SD, OUTPUT);
  digitalWrite(I2S_SD, HIGH); // Enable amplifier
  
  // Initialize ESP32-audioI2S
  audio = new Audio(true); // true = use I2S, false = use DAC
  audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio->setVolume(volume); // 0-21
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
    updateDisplay();
    return;
  }
  
  if (strlen(url) == 0 || strlen(name) == 0) {
    Serial.println("Error: Empty URL or name passed to startStream");
    updateDisplay();
    return;
  }
  
  // Validate URL format
  if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
    Serial.println("Error: Invalid URL format");
    updateDisplay();
    return;
  }
  
  strncpy(currentStream, url, sizeof(currentStream) - 1);
  currentStream[sizeof(currentStream) - 1] = '\0';
  strncpy(currentStreamName, name, sizeof(currentStreamName) - 1);
  currentStreamName[sizeof(currentStreamName) - 1] = '\0';
  isPlaying = true;         // Set playback status to playing
  
  // Use ESP32-audioI2S to play the stream
  if (audio) {
    audio->connecttohost(url);
  }
  
  updateDisplay();  // Refresh the display with new playback info
}

/**
 * @brief Stop the currently playing stream
 * Cleans up audio components and resets playback state
 */
void stopStream() {
  // Stop the audio playback
  if (audio) {
    audio->stopSong();
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
      file.println("[]");
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
  
  // Allocate buffer for file content
  std::unique_ptr<char[]> buf(new char[size + 1]);
  if (file.readBytes(buf.get(), size) != size) {
    Serial.println("Error: Failed to read playlist file");
    file.close();
    return;
  }
  buf[size] = '\0';
  file.close();
  
  // Parse the JSON content
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("Error: Failed to parse playlist JSON: ");
    Serial.println(error.c_str());
    
    // Try to recover by creating a backup and a new empty playlist
    Serial.println("Attempting to recover by creating backup and new playlist");
    if (SPIFFS.exists("/playlist.json.bak")) {
      SPIFFS.remove("/playlist.json.bak");
    }
    SPIFFS.rename("/playlist.json", "/playlist.json.bak");
    
    // Create a new empty playlist
    File newFile = SPIFFS.open("/playlist.json", "w");
    if (newFile) {
      newFile.println("[]");
      newFile.close();
      Serial.println("Created new empty playlist file");
    } else {
      Serial.println("Error: Failed to create new playlist file during recovery");
    }
    return;
  }
  
  if (!doc.is<JsonArray>()) {
    Serial.println("Error: Playlist JSON is not an array");
    
    // Try to recover by creating a backup and a new empty playlist
    Serial.println("Attempting to recover by creating backup and new playlist");
    if (SPIFFS.exists("/playlist.json.bak")) {
      SPIFFS.remove("/playlist.json.bak");
    }
    SPIFFS.rename("/playlist.json", "/playlist.json.bak");
    
    // Create a new empty playlist
    File newFile = SPIFFS.open("/playlist.json", "w");
    if (newFile) {
      newFile.println("[]");
      newFile.close();
      Serial.println("Created new empty playlist file");
    } else {
      Serial.println("Error: Failed to create new playlist file during recovery");
    }
    return;
  }
  
  JsonArray array = doc.as<JsonArray>();
  playlistCount = 0;
  
  for (JsonObject item : array) {
    if (playlistCount >= 20) {
      Serial.println("Warning: Playlist limit reached (20 entries)");
      break;
    }
    
    if (item.containsKey("name") && item.containsKey("url")) {
      const char* name = item["name"];
      const char* url = item["url"];
      
      // Validate name and URL
      if (name && url && strlen(name) > 0 && strlen(url) > 0) {
        // Validate URL format
        if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
          strncpy(playlist[playlistCount].name, name, sizeof(playlist[playlistCount].name) - 1);
          playlist[playlistCount].name[sizeof(playlist[playlistCount].name) - 1] = '\0';
          strncpy(playlist[playlistCount].url, url, sizeof(playlist[playlistCount].url) - 1);
          playlist[playlistCount].url[sizeof(playlist[playlistCount].url) - 1] = '\0';
          playlistCount++;
        } else {
          Serial.println("Warning: Skipping stream with invalid URL format");
        }
      } else {
        Serial.println("Warning: Skipping stream with empty name or URL");
      }
    }
  }
  
  Serial.print("Loaded ");
  Serial.print(playlistCount);
  Serial.println(" streams from playlist");
}

/**
 * @brief Save playlist to SPIFFS storage
 * Serializes the current playlist array to playlist.json
 */
void savePlaylist() {
  // Create JSON array
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.to<JsonArray>();
  
  // Add playlist entries
  for (int i = 0; i < playlistCount; i++) {
    // Validate URL format before saving
    if (strlen(playlist[i].url) == 0 || 
        (strncmp(playlist[i].url, "http://", 7) != 0 && strncmp(playlist[i].url, "https://", 8) != 0)) {
      Serial.println("Warning: Skipping stream with invalid URL format during save");
      continue;
    }
    
    JsonObject item = array.createNestedObject();
    item["name"] = playlist[i].name;
    item["url"] = playlist[i].url;
  }
  
  // Create backup of existing playlist file
  if (SPIFFS.exists("/playlist.json")) {
    if (SPIFFS.exists("/playlist.json.bak")) {
      SPIFFS.remove("/playlist.json.bak");
    }
    if (!SPIFFS.rename("/playlist.json", "/playlist.json.bak")) {
      Serial.println("Warning: Failed to create backup of playlist file");
    }
  }
  
  // Save to file
  File file = SPIFFS.open("/playlist.json", "w");
  if (!file) {
    Serial.println("Error: Failed to open playlist file for writing");
    
    // Try to restore from backup
    if (SPIFFS.exists("/playlist.json.bak")) {
      if (SPIFFS.rename("/playlist.json.bak", "/playlist.json")) {
        Serial.println("Restored playlist from backup");
      } else {
        Serial.println("Error: Failed to restore playlist from backup");
      }
    }
    return;
  }
  
  // Write JSON to file
  if (serializeJson(array, file) == 0) {
    Serial.println("Error: Failed to write playlist to file");
    file.close();
    
    // Try to restore from backup
    if (SPIFFS.exists("/playlist.json.bak")) {
      SPIFFS.remove("/playlist.json"); // Remove the failed file
      if (SPIFFS.rename("/playlist.json.bak", "/playlist.json")) {
        Serial.println("Restored playlist from backup");
      } else {
        Serial.println("Error: Failed to restore playlist from backup");
      }
    }
    return;
  }
  file.close();
  
  // Remove backup file after successful save
  if (SPIFFS.exists("/playlist.json.bak")) {
    SPIFFS.remove("/playlist.json.bak");
  }
  
  Serial.println("Saved playlist to file");
}

/**
 * @brief Load configuration from SPIFFS
 */
void loadConfig() {
  // No audio output configuration needed with ESP32-audioI2S
  if (!SPIFFS.exists("/config.json")) {
    Serial.println("Config file not found, using defaults");
    return;
  }
  
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Failed to open config file");
    return;
  }
  
  size_t size = file.size();
  if (size > 512) {
    Serial.println("Config file too large");
    file.close();
    return;
  }
  
  if (size == 0) {
    Serial.println("Config file is empty");
    file.close();
    return;
  }
  
  std::unique_ptr<char[]> buf(new char[size + 1]);
  if (file.readBytes(buf.get(), size) != size) {
    Serial.println("Failed to read config file");
    file.close();
    return;
  }
  buf[size] = '\0';
  file.close();
  
  // Config file exists but we don't need to parse audio output settings
  Serial.println("Loaded configuration from SPIFFS");
}

/**
 * @brief Save configuration to SPIFFS
 */
void saveConfig() {
  // No audio output configuration needed with ESP32-audioI2S
  Serial.println("Configuration saved to SPIFFS");
}


/**
 * @brief Initialize rotary encoder hardware
 * Configures pins and attaches interrupt handlers for the rotary encoder
 */
void setupRotaryEncoder() {
  // Configure rotary encoder pins with internal pull-up resistors
  pinMode(ROTARY_CLK, INPUT_PULLUP);  // Enable internal pull-up resistor
  pinMode(ROTARY_DT, INPUT_PULLUP);   // Enable internal pull-up resistor
  pinMode(ROTARY_SW, INPUT_PULLUP);   // Enable internal pull-up resistor
  
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
        if (audio) {
          audio->setVolume(volume / 5);  // ESP32-audioI2S uses 0-21 scale
        }
        sendStatusToClients();  // Notify clients of status change
      } else {
        // If not playing, select next item in playlist
        if (playlistCount > 0) {
          currentSelection = (currentSelection + 1) % playlistCount;
        }
      }
    } else if (diff < 0) {
      // Process counter-clockwise rotation
      // Rotate counter-clockwise - volume down or previous item
      if (isPlaying) {
        // If playing, decrease volume by 5% (capped at 0%)
        volume = max(0, volume - 5);
        if (audio) {
          audio->setVolume(volume / 5);  // ESP32-audioI2S uses 0-21 scale
        }
        sendStatusToClients();  // Notify clients of status change
      } else {
        // If not playing, select previous item in playlist
        if (playlistCount > 0) {
          currentSelection = (currentSelection - 1 + playlistCount) % playlistCount;
        }
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
        sendStatusToClients();  // Notify clients of status change
      } else {
        // If not playing, start playback of selected stream
        startStream(playlist[currentSelection].url, playlist[currentSelection].name);
        sendStatusToClients();  // Notify clients of status change
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
  // If playlist file doesn't exist, create a default empty one
  if (!SPIFFS.exists("/playlist.json")) {
    File file = SPIFFS.open("/playlist.json", "w");
    if (file) {
      file.println("[]");
      file.close();
    }
  }
  
  // Return JSON format (default)
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
 * Supports both direct JSON POST and file upload mechanisms
 */
void handlePostStreams() {
  String jsonData;
  
  // Check if this is a file upload
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("UploadStart: %s\n", upload.filename.c_str());
    return;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    return;
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("UploadEnd: %s (%d)\n", upload.filename.c_str(), (int)upload.totalSize);
    
    // Get data from upload buffer
    if (upload.buf == nullptr || upload.currentSize == 0) {
      server.send(400, "text/plain", "No data received");
      return;
    }
    
    jsonData = String((char*)upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("Upload Aborted");
    server.send(500, "text/plain", "Upload Aborted");
    return;
  } else {
    // Handle regular POST data
    if (!server.hasArg("plain")) {
      server.send(400, "text/plain", "Missing JSON data");
      return;
    }
    
    jsonData = server.arg("plain");
  }
  
  // Validate JSON data
  if (jsonData.length() == 0) {
    server.send(400, "text/plain", "Empty JSON data");
    return;
  }
  
  jsonData.trim();
  if (!jsonData.startsWith("[") || !jsonData.endsWith("]")) {
    server.send(400, "text/plain", "Invalid JSON format");
    return;
  }
  
  // Parse JSON and update playlist array
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.print("Error: Failed to parse playlist JSON: ");
    Serial.println(error.c_str());
    server.send(400, "text/plain", "Invalid JSON format");
    return;
  }
  
  if (!doc.is<JsonArray>()) {
    Serial.println("Error: Playlist JSON is not an array");
    server.send(400, "text/plain", "Invalid JSON format");
    return;
  }
  
  JsonArray array = doc.as<JsonArray>();
  playlistCount = 0;
  
  for (JsonObject item : array) {
    if (playlistCount >= 20) {
      Serial.println("Warning: Playlist limit reached (20 entries)");
      break;
    }
    
    if (item.containsKey("name") && item.containsKey("url")) {
      const char* name = item["name"];
      const char* url = item["url"];
      
      // Validate name and URL
      if (name && url && strlen(name) > 0 && strlen(url) > 0) {
        // Validate URL format
        if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
          strncpy(playlist[playlistCount].name, name, sizeof(playlist[playlistCount].name) - 1);
          playlist[playlistCount].name[sizeof(playlist[playlistCount].name) - 1] = '\0';
          strncpy(playlist[playlistCount].url, url, sizeof(playlist[playlistCount].url) - 1);
          playlist[playlistCount].url[sizeof(playlist[playlistCount].url) - 1] = '\0';
          playlistCount++;
        } else {
          Serial.println("Warning: Skipping stream with invalid URL format");
        }
      } else {
        Serial.println("Warning: Skipping stream with empty name or URL");
      }
    }
  }
  
  // Save the playlist using the savePlaylist function
  savePlaylist();
  
  // Send appropriate response based on upload type
  if (upload.status == UPLOAD_FILE_END) {
    server.send(200, "text/plain", "JSON playlist updated successfully");
  } else {
    server.send(200, "text/plain", "OK");
  }
}


/**
 * @brief Handle play request
 * Starts playing a stream with the given URL and name
 */
void handlePlay() {
  if (server.hasArg("plain")) {
    // Handle JSON payload
    String json = server.arg("plain");
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }
    
    if (!doc.containsKey("url") || !doc.containsKey("name")) {
      server.send(400, "text/plain", "Missing required parameters: url and name");
      return;
    }
    
    String url = doc["url"].as<String>();
    String name = doc["name"].as<String>();
    
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
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "text/plain", "OK");
  } else if (server.hasArg("url") && server.hasArg("name")) {
    // Handle form data
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
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing required parameters: url and name");
    return;
  }
}

/**
 * @brief Handle stop request
 * Stops the currently playing stream
 */
void handleStop() {
  stopStream();
  sendStatusToClients();  // Notify clients of status change
  server.send(200, "text/plain", "OK");
}

/**
 * @brief Handle volume request
 * Sets the volume level
 */
void handleVolume() {
  if (server.hasArg("plain")) {
    // Handle JSON payload
    String json = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }
    
    if (!doc.containsKey("volume")) {
      server.send(400, "text/plain", "Missing required parameter: volume");
      return;
    }
    
    int newVolume = doc["volume"];
    
    if (newVolume < 0 || newVolume > 100) {
      server.send(400, "text/plain", "Volume must be between 0 and 100");
      return;
    }
    
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume / 5);  // ESP32-audioI2S uses 0-21 scale
    }
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "text/plain", "OK");
  } else if (server.hasArg("volume")) {
    // Handle form data
    String vol = server.arg("volume");
    int newVolume = vol.toInt();
    
    if (newVolume < 0 || newVolume > 100) {
      server.send(400, "text/plain", "Volume must be between 0 and 100");
      return;
    }
    
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume / 5);  // ESP32-audioI2S uses 0-21 scale
    }
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing required parameter: volume");
    return;
  }
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
 * @brief Send status to all connected WebSocket clients
 */
void sendStatusToClients() {
  String status = "{";
  status += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
  status += "\"currentStream\":\"" + String(currentStream) + "\",";
  status += "\"currentStreamName\":\"" + String(currentStreamName) + "\",";
  status += "\"volume\":" + String(volume);
  status += "}";
  
  webSocket.broadcastTXT(status);
}

/**
 * @brief Handle WebSocket events
 * Processes WebSocket connection, disconnection, and message events
 * @param num Client number
 * @param type Event type (connected, disconnected, text message, etc.)
 * @param payload Message payload for text messages
 * @param length Length of the payload
 */
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_CONNECTED:
      Serial.printf("WebSocket client #%u connected from %d.%d.%d.%d\n", num,
                    webSocket.remoteIP(num)[0], webSocket.remoteIP(num)[1],
                    webSocket.remoteIP(num)[2], webSocket.remoteIP(num)[3]);
      // Send current status to newly connected client
      sendStatusToClients();
      break;
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client #%u disconnected\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("WebSocket client #%u text: %s\n", num, payload);
      // Handle ping messages from client
      if (length == 4 && strncmp((char*)payload, "ping", 4) == 0) {
        // Respond with pong
        webSocket.sendTXT(num, "pong");
      }
      break;
    default:
      break;
  }
}

/**
 * @brief Update the OLED display with current status
 * Shows playback status, current stream, volume level, and playlist selection
 */
void updateDisplay() {
  display.clearDisplay();  // Clear the display
  
  if (isPlaying) {
    // Display when playing
    display.setTextSize(2);  // Larger font for status
    display.setCursor(0, 0);
    display.println("PLAYING");
    display.setTextSize(1);  // Normal font for stream name
    display.setCursor(0, 18);
    display.println(currentStreamName);
    // Display volume on the last line
    display.setCursor(0, 50);
    display.print("[");
    for (int i = 0; i < 20; i++) {
      if (i < volume / 5) {
        display.print("|");
      } else {
        display.print(" ");
      }
    }
    display.print("] ");
    display.print(volume);
    display.println("%");
  } else {
    // Display when stopped
    display.setTextSize(2);  // Larger font for status
    display.setCursor(0, 0);
    display.println("STOPPED");
    display.setTextSize(1);  // Normal font for other text
    if (playlistCount > 0 && currentSelection < playlistCount) {
      // Show selected playlist item
      display.setCursor(0, 18);
      display.println(playlist[currentSelection].name);
      display.setCursor(0, 30);
      display.println("Press to play");
    } else {
      // Show message when no streams available
      display.setCursor(0, 18);
      display.println("No streams");
    }
    // Display volume on the last line
    display.setCursor(0, 50);
    display.print("[");
    for (int i = 0; i < 20; i++) {
      if (i < volume / 5) {
        display.print("|");
      } else {
        display.print(" ");
      }
    }
    display.print("] ");
    display.print(volume);
    display.println("%");
  }
  
  display.display();  // Send buffer to display
}

/**
 * @brief Handle MPD client connections and commands
 * Processes commands from MPD clients
 */
void handleMPDClient() {
  if (mpdServer.hasClient()) {
    if (!mpdClient || !mpdClient.connected()) {
      if (mpdClient) {
        mpdClient.stop();
      }
      mpdClient = mpdServer.available();
      // Send MPD welcome message
      mpdClient.println("OK MPD 0.20.0");
    } else {
      // Reject connection if we already have a client
      WiFiClient rejectedClient = mpdServer.available();
      rejectedClient.stop();
    }
  }
  
  if (mpdClient && mpdClient.connected()) {
    if (mpdClient.available()) {
      String command = mpdClient.readStringUntil('\n');
      command.trim();
      Serial.println("MPD Command: " + command);
      handleMPDCommand(command);
    }
  }
}

/**
 * @brief Generate MPD OK response
 * @return OK response string
 */
String mpdResponseOK() {
  return "OK\n";
}

/**
 * @brief Generate MPD error response
 * @param message Error message
 * @return Error response string
 */
String mpdResponseError(const String& message) {
  return "ACK {" + message + "}\n";
}

/**
 * @brief Handle MPD commands
 * Processes MPD protocol commands
 * @param command The command string to process
 */
void handleMPDCommand(const String& command) {
  if (command.startsWith("play")) {
    // Play command
    if (playlistCount > 0) {
      int index = -1;
      if (command.length() > 5) {
        index = command.substring(5).toInt();
      }
      
      if (index >= 0 && index < playlistCount) {
        startStream(playlist[index].url, playlist[index].name);
      } else if (playlistCount > 0 && currentSelection < playlistCount) {
        startStream(playlist[currentSelection].url, playlist[currentSelection].name);
      }
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
    }
  } else if (command.startsWith("stop")) {
    // Stop command
    stopStream();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("pause")) {
    // Pause command (treat as stop for simplicity)
    stopStream();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("status")) {
    // Status command
    mpdClient.println("volume: " + String(volume));
    mpdClient.println("state: " + String(isPlaying ? "play" : "stop"));
    if (isPlaying && strlen(currentStreamName) > 0) {
      mpdClient.println("song: " + String(currentSelection));
      mpdClient.println("songid: " + String(currentSelection));
      mpdClient.println("title: " + String(currentStreamName));
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("currentsong")) {
    // Current song command
    if (isPlaying && strlen(currentStreamName) > 0) {
      mpdClient.println("file: " + String(currentStream));
      mpdClient.println("Title: " + String(currentStreamName));
      mpdClient.println("Id: " + String(currentSelection));
      mpdClient.println("Pos: " + String(currentSelection));
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistinfo")) {
    // Playlist info command
    for (int i = 0; i < playlistCount; i++) {
      mpdClient.println("file: " + String(playlist[i].url));
      mpdClient.println("Title: " + String(playlist[i].name));
      mpdClient.println("Id: " + String(i));
      mpdClient.println("Pos: " + String(i));
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistid")) {
    // Playlist ID command
    for (int i = 0; i < playlistCount; i++) {
      mpdClient.println("file: " + String(playlist[i].url));
      mpdClient.println("Title: " + String(playlist[i].name));
      mpdClient.println("Id: " + String(i));
      mpdClient.println("Pos: " + String(i));
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("lsinfo")) {
    // List info command
    for (int i = 0; i < playlistCount; i++) {
      mpdClient.println("file: " + String(playlist[i].url));
      mpdClient.println("Title: " + String(playlist[i].name));
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("setvol")) {
    // Set volume command
    if (command.length() > 7) {
      int newVolume = command.substring(7).toInt();
      if (newVolume >= 0 && newVolume <= 100) {
        volume = newVolume;
        if (audio) {
          audio->setVolume(volume / 5);  // ESP32-audioI2S uses 0-21 scale
        }
        sendStatusToClients();  // Notify WebSocket clients
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("Volume out of range"));
      }
    } else {
      mpdClient.print(mpdResponseError("Missing volume value"));
    }
  } else if (command.startsWith("next")) {
    // Next command
    if (playlistCount > 0) {
      currentSelection = (currentSelection + 1) % playlistCount;
      if (isPlaying) {
        startStream(playlist[currentSelection].url, playlist[currentSelection].name);
      }
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
    }
  } else if (command.startsWith("previous")) {
    // Previous command
    if (playlistCount > 0) {
      currentSelection = (currentSelection - 1 + playlistCount) % playlistCount;
      if (isPlaying) {
        startStream(playlist[currentSelection].url, playlist[currentSelection].name);
      }
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
    }
  } else if (command.startsWith("clear")) {
    // Clear command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("add")) {
    // Add command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("delete")) {
    // Delete command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("load")) {
    // Load command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("save")) {
    // Save command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("outputs")) {
    // Outputs command - with ESP32-audioI2S, we only have I2S output
    mpdClient.println("outputid: 0");
    mpdClient.println("outputname: I2S (External DAC)");
    mpdClient.println("outputenabled: 1");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("disableoutput")) {
    // Disable output command
    if (command.length() > 13) {
      int outputId = command.substring(14).toInt();
      // We don't actually disable outputs, just acknowledge the command
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("Missing output ID"));
    }
  } else if (command.startsWith("enableoutput")) {
    // Enable output command
    if (command.length() > 12) {
      int outputId = command.substring(13).toInt();
      if (outputId == 0) {
        // Only output 0 (I2S) is supported with ESP32-audioI2S
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("Invalid output ID"));
      }
    } else {
      mpdClient.print(mpdResponseError("Missing output ID"));
    }
  } else if (command.startsWith("commands")) {
    // Commands command
    mpdClient.println("command: add");
    mpdClient.println("command: clear");
    mpdClient.println("command: currentsong");
    mpdClient.println("command: delete");
    mpdClient.println("command: disableoutput");
    mpdClient.println("command: enableoutput");
    mpdClient.println("command: load");
    mpdClient.println("command: lsinfo");
    mpdClient.println("command: next");
    mpdClient.println("command: outputs");
    mpdClient.println("command: pause");
    mpdClient.println("command: play");
    mpdClient.println("command: playlistid");
    mpdClient.println("command: playlistinfo");
    mpdClient.println("command: previous");
    mpdClient.println("command: save");
    mpdClient.println("command: setvol");
    mpdClient.println("command: status");
    mpdClient.println("command: stop");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("notcommands")) {
    // Not commands command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("tagtypes")) {
    // Tag types command
    mpdClient.println("tagtype: Artist");
    mpdClient.println("tagtype: Album");
    mpdClient.println("tagtype: Title");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("idle")) {
    // Idle command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("noidle")) {
    // Noidle command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("close")) {
    // Close command
    mpdClient.print(mpdResponseOK());
    mpdClient.stop();
  } else if (command.length() == 0) {
    // Empty command
    mpdClient.print(mpdResponseOK());
  } else {
    // Unknown command
    mpdClient.print(mpdResponseError("Unknown command"));
  }
}
