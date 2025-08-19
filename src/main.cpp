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
#define MAX_WIFI_NETWORKS 5
char ssid[MAX_WIFI_NETWORKS][64] = {""};
char password[MAX_WIFI_NETWORKS][64] = {""};
int wifiNetworkCount = 0;

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
/**
 * @brief Player state variables
 * Track current playback status, stream information, and volume level
 */
char currentStream[256] = "";      ///< URL of currently playing stream
char currentStreamName[128] = "";  ///< Name of currently playing stream
char streamTitle[128] = "";        ///< Current stream title
int bitrate = 0;                   ///< Current stream bitrate
volatile bool isPlaying = false;   ///< Playback status flag (volatile for core synchronization)
int volume = 50;                   ///< Volume level (0-100)
unsigned long lastActivityTime = 0; ///< Last activity timestamp
bool displayOn = true;             ///< Display on/off status

/**
 * @brief Audio processing components
 * These pointers manage the audio streaming pipeline
 */
Audio *audio = nullptr;                     ///< Audio instance for ESP32-audioI2S
bool audioConnected = false;                ///< Audio connection status flag

// Forward declaration of sendStatusToClients function
void sendStatusToClients();
// Forward declaration of updateDisplay function
void updateDisplay();

/**
 * @brief Audio stream title callback function
 * This function is called by the Audio library when stream title information is available
 * @param info Pointer to the stream title information
 */
void audio_showstreamtitle(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("Stream title: ");
    Serial.println(info);
    
    // Update stream title if it has changed
    if (strcmp(streamTitle, info) != 0) {
      strncpy(streamTitle, info, sizeof(streamTitle) - 1);
      streamTitle[sizeof(streamTitle) - 1] = '\0';
      updateDisplay();
      sendStatusToClients();  // Notify clients of title change
    }
  }
}

/**
 * @brief Audio station name callback function
 * This function is called by the Audio library when station name information is available
 * @param info Pointer to the station name information
 */
void audio_showstation(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("Station name: ");
    Serial.println(info);
    
    // Update current stream name if it has changed and we're not already using a custom name
    if (strcmp(currentStreamName, info) != 0) {
      strncpy(currentStreamName, info, sizeof(currentStreamName) - 1);
      currentStreamName[sizeof(currentStreamName) - 1] = '\0';
      updateDisplay();
      sendStatusToClients();  // Notify clients of station name change
    }
  }
}

/**
 * @brief Audio bitrate callback function
 * This function is called by the Audio library when bitrate information is available
 * @param info Pointer to the bitrate information
 */
void audio_bitrate(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("Bitrate: ");
    Serial.println(info);
    
    // Convert string to integer
    int newBitrate = atoi(info);
    
    // Update bitrate if it has changed
    if (newBitrate > 0 && newBitrate != bitrate) {
      bitrate = newBitrate;
      updateDisplay();
      sendStatusToClients();  // Notify clients of bitrate change
    }
  }
}

/**
 * @brief I2S pin configuration for audio output
 * Defines the pin mapping for I2S audio interface
 */
#define I2S_DOUT      26  ///< I2S Data Out pin
#define I2S_BCLK      27  ///< I2S Bit Clock pin
#define I2S_LRC       25  ///< I2S Left/Right Clock pin

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
#define ROTARY_DT  19  ///< Rotary encoder data pin (quadrature signal B)
#define ROTARY_SW  23  ///< Rotary encoder switch pin (push button)

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

// Constants
#define MAX_PLAYLIST_SIZE 20
#define VALIDATE_URL(url) (url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0))

/**
 * @brief Playlist storage and management variables
 * Array to store playlist entries and tracking variables
 */
StreamInfo playlist[MAX_PLAYLIST_SIZE];    ///< Array of stream information (max 20 entries)
int playlistCount = 0;      ///< Number of valid entries in the playlist
int currentSelection = 0;   ///< Currently selected item in the playlist

/**
 * @brief Task management handles
 * Used to manage and reference created FreeRTOS tasks
 */
TaskHandle_t audioTaskHandle = NULL;  ///< Handle for the audio processing task

// Function declarations
void setupAudioOutput();
void startStream(const char* url = nullptr, const char* name = nullptr);
void stopStream();
void loadPlaylist();
void savePlaylist();
void setupRotaryEncoder();
void rotaryISR();
void updateDisplay();
void handleRotary();
void handleDisplayTimeout();
void sendStatusToClients();
void audioTask(void *pvParameters);
void forceDisplayUpdate();

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
void handleWiFiStatus();
void handleWiFiConfigAPI();
void loadWiFiCredentials();
void saveWiFiCredentials();

// WebSocket handlers
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

/**
 * @brief Arduino setup function
 * Initializes all system components and starts the web server
 */
void setup() {
  Serial.begin(115200);
  
  // Initialize SPIFFS with error recovery
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    // Try to reformat SPIFFS
    if (!SPIFFS.format()) {
      Serial.println("ERROR: Failed to format SPIFFS");
      return;
    }
    // Try to mount again after formatting
    if (!SPIFFS.begin(true)) {
      Serial.println("ERROR: Failed to mount SPIFFS after formatting");
      return;
    }
    Serial.println("SPIFFS formatted and mounted successfully");
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
  
  // Load WiFi credentials with error recovery
  loadWiFiCredentials();
  
  // Connect to WiFi with improved error handling
  bool connected = false;
  if (wifiNetworkCount > 0) {
    WiFi.setHostname("NetTuner");
    
    // Try each configured network
    for (int i = 0; i < wifiNetworkCount; i++) {
      if (strlen(ssid[i]) > 0) {
        Serial.printf("Attempting to connect to %s...\n", ssid[i]);
        WiFi.begin(ssid[i], password[i]);
        int wifiAttempts = 0;
        const int maxAttempts = 15; // Increased attempts per network
        while (WiFi.status() != WL_CONNECTED && wifiAttempts < maxAttempts) {
          delay(500);
          Serial.print(".");
          wifiAttempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("Connected to %s\n", ssid[i]);
          connected = true;
          break;
        } else {
          Serial.printf("Failed to connect to %s\n", ssid[i]);
          // Reset WiFi before trying next network
          WiFi.disconnect();
          delay(1000);
        }
      }
    }
    
    if (connected) {
      Serial.println("Connected to WiFi");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP().toString());
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi Connected");
      display.println(WiFi.localIP().toString());
      display.display();
    } else {
      Serial.println("Failed to connect to any configured WiFi network");
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
    
    // Start WiFi access point mode with error handling
    if (WiFi.softAP("NetTuner-Setup")) {
      Serial.println("Access Point Started");
      Serial.print("AP IP Address: ");
      Serial.println(WiFi.softAPIP().toString());
      display.println("AP: NetTuner-Setup");
      display.println(WiFi.softAPIP().toString());
      display.display();
    } else {
      Serial.println("Failed to start Access Point");
      display.println("AP Start Failed");
      display.display();
    }
  }
  
  // Setup audio output with error handling
  setupAudioOutput();
    
  // Setup rotary encoder with error handling
  setupRotaryEncoder();
  
  // Load playlist with error recovery
  loadPlaylist();
  
  // Validate loaded playlist
  if (playlistCount < 0 || playlistCount > MAX_PLAYLIST_SIZE) {
    Serial.println("Warning: Invalid playlist count detected, resetting to 0");
    playlistCount = 0;
  }
  
  if (currentSelection < 0 || currentSelection >= playlistCount) {
    currentSelection = 0;
  }
  
  
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
  server.on("/api/wifistatus", HTTP_GET, handleWiFiStatus);
  server.on("/api/wificonfig", HTTP_GET, handleWiFiConfigAPI);
   
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/styles.css", SPIFFS, "/styles.css");
  server.serveStatic("/scripts.js", SPIFFS, "/scripts.js");
  
  // Start server
  server.begin();
  Serial.println("Web server started");
  
  // Setup WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Start MPD server
  mpdServer.begin();
  Serial.println("MPD server started");
  
  // Create audio task on core 0 with error checking
  BaseType_t result = xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, &audioTaskHandle, 0);
  if (result != pdPASS) {
    Serial.println("ERROR: Failed to create AudioTask");
  } else {
    Serial.println("AudioTask created successfully");
  }
  
  // Update display
  updateDisplay();
}

/**
 * @brief Audio task function
 * Handles audio streaming on core 0
 */
void audioTask(void *pvParameters) {
  while (true) {
    // Process audio streaming with error handling
    if (audio) {
      audio->loop();
    }
    // Very small delay to prevent busy waiting but allow frequent processing
    delay(1);
  }
}

/**
 * @brief Arduino main loop function
 * Handles web server requests, WebSocket events, rotary encoder input, and MPD commands
 */
void loop() {
  static unsigned long streamStoppedTime = 0;

  handleRotary();          // Process rotary encoder input
  server.handleClient();   // Process incoming web requests
  webSocket.loop();        // Process WebSocket events
  handleMPDClient();       // Process MPD commands
  
  // Periodically update display for scrolling text animation
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 100) {  // Update every 100ms for smooth scrolling
    if (displayOn) {  // Only update if display is on
      updateDisplay();
    }
    lastDisplayUpdate = millis();
  }
  
  // Check audio connection status with improved error recovery
  if (audio) {
    // Check if audio is still connected
    if (isPlaying) {
      if (!audio->isRunning()) {
        Serial.println("Audio stream stopped unexpectedly");
        // Attempt to restart the stream if it was playing
        if (strlen(currentStream) > 0) {
          // Wait 1 second before attempting to restart (non-blocking)
          if (streamStoppedTime == 0) {
            // First time detecting the stream has stopped
            streamStoppedTime = millis();
            Serial.println("Waiting 1 second before restart attempt...");
          } else if (millis() - streamStoppedTime >= 1000) {
            // 1 second has passed, attempt to restart
            Serial.println("Attempting to restart stream...");
            // With the updated startStream function, we can now call it without parameters
            // to resume the current stream
            startStream();
            streamStoppedTime = 0; // Reset the timer
          }
        }
      } else {
        // Stream is running, reset the stopped time
        streamStoppedTime = 0;
        // Update bitrate if it has changed
        int newBitrate = audio->getBitRate();
        if (newBitrate > 0 && newBitrate != bitrate) {
          bitrate = newBitrate;
          // Update the bitrate on display
          updateDisplay();
          // Notify clients of bitrate change
          sendStatusToClients();
        }
      }
    }
  }
  
  // Periodic cleanup with error recovery
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 30000) {  // Every 30 seconds
    lastCleanup = millis();
    // Force cleanup of any stale connections
    if (mpdClient && !mpdClient.connected()) {
      mpdClient.stop();
    }
    
    // Check and recover from potential WiFi disconnections
    if (wifiNetworkCount > 0 && WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, attempting to reconnect...");
      // Try to reconnect to WiFi
      for (int i = 0; i < wifiNetworkCount; i++) {
        if (strlen(ssid[i]) > 0) {
          WiFi.begin(ssid[i], password[i]);
          int wifiAttempts = 0;
          const int maxAttempts = 5;
          while (WiFi.status() != WL_CONNECTED && wifiAttempts < maxAttempts) {
            delay(500);
            wifiAttempts++;
          }
          
          if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Reconnected to %s\n", ssid[i]);
            // Update display with new IP
            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("WiFi Reconnected");
            display.println(WiFi.localIP().toString());
            display.display();
            delay(2000);
            updateDisplay();
            break;
          }
        }
      }
    }
  }
  // Handle display timeout
  handleDisplayTimeout();
  // Small delay to prevent busy waiting
  delay(10);
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
 * @brief Handle WiFi configuration API request
 * Returns the current WiFi configuration as JSON
 */
void handleWiFiConfigAPI() {
  String json = "[";
  
  for (int i = 0; i < wifiNetworkCount; i++) {
    if (i > 0) json += ",";
    json += "\"" + String(ssid[i]) + "\"";
  }
  
  json += "]";
  server.send(200, "application/json", json);
}



/**
 * @brief Handle WiFi network scan
 * Returns a list of available WiFi networks as JSON
 */
void handleWiFiScan() {
  int n = WiFi.scanNetworks();
  String json = "{";
  json += "\"networks\":[";
  
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i));
    json += "}";
  }
  json += "],";
  
  // Add configured networks
  json += "\"configured\":[";
  for (int i = 0; i < wifiNetworkCount; i++) {
    if (i > 0) json += ",";
    json += "\"" + String(ssid[i]) + "\"";
  }
  json += "]";
  json += "}";
  
  server.send(200, "application/json", json);
}

/**
 * @brief Handle WiFi configuration save
 * Saves WiFi credentials to SPIFFS
 */
void handleWiFiSave() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON data\"}");
    return;
  }
  
  String json = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  // Handle both single network and multiple networks
  wifiNetworkCount = 0;
    
  if (doc.containsKey("ssid")) {
    if (doc["ssid"].is<JsonArray>()) {
      JsonArray ssidArray = doc["ssid"];
      JsonArray passwordArray = doc["password"].as<JsonArray>();
        
      for (int i = 0; i < (int)ssidArray.size() && i < MAX_WIFI_NETWORKS; i++) {
        String newSSID = ssidArray[i].as<String>();
        if (newSSID.length() == 0 || newSSID.length() >= sizeof(ssid[i])) {
          server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid SSID length\"}");
          return;
        }
          
        strncpy(ssid[i], newSSID.c_str(), sizeof(ssid[i]) - 1);
        ssid[i][sizeof(ssid[i]) - 1] = '\0';
          
        if (i < (int)passwordArray.size()) {
          String newPassword = passwordArray[i].as<String>();
          if (newPassword.length() >= sizeof(password[i])) {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Password too long\"}");
            return;
          }
          strncpy(password[i], newPassword.c_str(), sizeof(password[i]) - 1);
          password[i][sizeof(password[i]) - 1] = '\0';
        } else {
          password[i][0] = '\0';
        }
          
        wifiNetworkCount++;
      }
    } else {
      // Single network for backward compatibility
      String newSSID = doc["ssid"].as<String>();
      if (newSSID.length() == 0 || newSSID.length() >= sizeof(ssid[0])) {
        server.send(400, "text/plain", "Invalid SSID length");
        return;
      }
        
      strncpy(ssid[0], newSSID.c_str(), sizeof(ssid[0]) - 1);
      ssid[0][sizeof(ssid[0]) - 1] = '\0';
      wifiNetworkCount = 1;
        
      if (doc.containsKey("password")) {
        String newPassword = doc["password"].as<String>();
        // Validate password length
        if (newPassword.length() >= sizeof(password[0])) {
          server.send(400, "text/plain", "Password too long");
          return;
        }
        strncpy(password[0], newPassword.c_str(), sizeof(password[0]) - 1);
        password[0][sizeof(password[0]) - 1] = '\0';
      } else {
        password[0][0] = '\0';
      }
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing SSID\"}");
    return;
  }
  
  saveWiFiCredentials();
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"WiFi configuration saved\"}");
}

/**
 * @brief Handle WiFi status request
 * Returns the current WiFi connection status as JSON
 */
void handleWiFiStatus() {
  String json = "{";
  
  if (WiFi.status() == WL_CONNECTED) {
    json += "\"connected\":true,";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
  } else {
    json += "\"connected\":false";
  }
  
  json += "}";
  server.send(200, "application/json", json);
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
  if (!buf) {
    Serial.println("Error: Failed to allocate memory for WiFi config file");
    file.close();
    return;
  }
  
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
    // Handle both single SSID and array of SSIDs
    if (doc["ssid"].is<JsonArray>()) {
      JsonArray ssidArray = doc["ssid"];
      wifiNetworkCount = 0;
      for (JsonVariant value : ssidArray) {
        if (wifiNetworkCount >= MAX_WIFI_NETWORKS) break;
        strncpy(ssid[wifiNetworkCount], value.as<const char*>(), sizeof(ssid[wifiNetworkCount]) - 1);
        ssid[wifiNetworkCount][sizeof(ssid[wifiNetworkCount]) - 1] = '\0';
        wifiNetworkCount++;
      }
    } else {
      // Single SSID for backward compatibility
      strncpy(ssid[0], doc["ssid"], sizeof(ssid[0]) - 1);
      ssid[0][sizeof(ssid[0]) - 1] = '\0';
      wifiNetworkCount = 1;
    }
  }
  
  if (doc.containsKey("password")) {
    // Handle both single password and array of passwords
    if (doc["password"].is<JsonArray>()) {
      JsonArray passwordArray = doc["password"];
      for (int i = 0; i < wifiNetworkCount && i < MAX_WIFI_NETWORKS; i++) {
        if (i < (int)passwordArray.size()) {
          const char* pwd = passwordArray[i];
          if (pwd) {
            strncpy(password[i], pwd, sizeof(password[i]) - 1);
            password[i][sizeof(password[i]) - 1] = '\0';
          } else {
            password[i][0] = '\0';
          }
        } else {
          password[i][0] = '\0';
        }
      }
    } else {
      // Single password for backward compatibility
      const char* pwd = doc["password"];
      if (pwd) {
        strncpy(password[0], pwd, sizeof(password[0]) - 1);
        password[0][sizeof(password[0]) - 1] = '\0';
      } else {
        password[0][0] = '\0';
      }
      // Clear other passwords
      for (int i = 1; i < MAX_WIFI_NETWORKS; i++) {
        password[i][0] = '\0';
      }
    }
  }
  
  Serial.println("Loaded WiFi credentials from SPIFFS");
  for (int i = 0; i < wifiNetworkCount; i++) {
    Serial.printf("SSID[%d]: %s\n", i, ssid[i]);
    // Only print password if it exists and is not empty
    if (strlen(password[i]) > 0) {
      Serial.println("Password: [REDACTED]");
    } else {
      Serial.println("Password: [NONE]");
    }
  }
}

/**
 * @brief Save WiFi credentials to SPIFFS
 */
void saveWiFiCredentials() {
  DynamicJsonDocument doc(1024); // Increased size for multiple networks
  
  // Save SSIDs as array
  JsonArray ssidArray = doc.createNestedArray("ssid");
  for (int i = 0; i < wifiNetworkCount; i++) {
    ssidArray.add(ssid[i]);
  }
  
  // Save passwords as array
  JsonArray passwordArray = doc.createNestedArray("password");
  for (int i = 0; i < wifiNetworkCount; i++) {
    passwordArray.add(password[i]);
  }
  
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
  // Initialize ESP32-audioI2S
  audio = new Audio(false); // false = use I2S, true = use DAC
  audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio->setVolume(volume * 21 / 100); // Convert 0-100 to 0-21 scale
  audio->setBufsize(65536, 0); // Increased buffer size to 64KB for better streaming performance
}

/**
 * @brief Start streaming an audio stream
 * Stops any currently playing stream and begins playing a new one
 * If called without parameters, resumes playback of currentStream if available
 * @param url URL of the audio stream to play (optional)
 * @param name Human-readable name of the stream (optional)
 */
void startStream(const char* url, const char* name) {
  bool resume = false;
  // Stop any currently playing stream
  if (audio) {
    // Stop first
    audio->stopSong();
  }

  // If no URL provided, check if we have a current stream to resume
  if (!url || strlen(url) == 0) {
    if (strlen(currentStream) > 0) {
      // Resume playback of current stream
      url = currentStream;
      // Use current name if available, otherwise use a default
      if (!name || strlen(name) == 0) {
        name = (strlen(currentStreamName) > 0) ? currentStreamName : "Unknown Station";
      }
      // We are resuming playback
      resume = true;
    } else {
      Serial.println("Error: No URL provided and no current stream to resume");
      updateDisplay();
      return;
    }
  }

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
  
  // Keep the stream url and name if they are new
  if (not resume) {
    strncpy(currentStream, url, sizeof(currentStream) - 1);
    currentStream[sizeof(currentStream) - 1] = '\0';
    strncpy(currentStreamName, name, sizeof(currentStreamName) - 1);
    currentStreamName[sizeof(currentStreamName) - 1] = '\0';
  };

  // Set playback status to playing
  isPlaying = true;
  
  // Use ESP32-audioI2S to play the stream
  if (audio) {
    audioConnected = audio->connecttohost(url);
    if (!audioConnected) {
      Serial.println("Error: Failed to connect to audio stream");
      isPlaying = false;
      bitrate = 0;
    } else {
      isPlaying = true;
      Serial.println("Successfully connected to audio stream");
    }
  }
  
  updateDisplay();  // Refresh the display with new playback info
  sendStatusToClients();  // Notify clients of status change
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
  audioConnected = false;
  
  isPlaying = false;             // Set playback status to stopped
  currentStream[0] = '\0';       // Clear current stream URL
  currentStreamName[0] = '\0';   // Clear current stream name
  streamTitle[0] = '\0';         // Clear stream title
  bitrate = 0;                   // Clear bitrate
  
  updateDisplay();  // Refresh the display
  sendStatusToClients();  // Notify clients of status change
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
  if (!buf) {
    Serial.println("Error: Failed to allocate memory for playlist file");
    file.close();
    return;
  }
  
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
    if (SPIFFS.rename("/playlist.json", "/playlist.json.bak")) {
      Serial.println("Created backup of playlist file");
    }
    
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
        if (VALIDATE_URL(url)) {
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
        !VALIDATE_URL(playlist[i].url)) {
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
  
  // Write JSON to file with better error handling
  size_t bytesWritten = serializeJson(array, file);
  if (bytesWritten == 0) {
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
  file.close();
  
  // Remove backup file after successful save
  if (SPIFFS.exists("/playlist.json.bak")) {
    SPIFFS.remove("/playlist.json.bak");
  }
  
  Serial.println("Saved playlist to file");
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
          audio->setVolume(volume * 21 / 100);  // ESP32-audioI2S uses 0-21 scale
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
          audio->setVolume(volume * 21 / 100);  // ESP32-audioI2S uses 0-21 scale
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
    lastActivityTime = millis(); // Update activity time
    if (!displayOn) {
      displayOn = true;
      updateDisplay(); // Turn display back on and update
    }
    
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
 * @brief Handle display timeout
 * Turns off the display after a period of inactivity when not playing
 */
void handleDisplayTimeout() {
  const unsigned long DISPLAY_TIMEOUT = 30000; // 30 seconds
  unsigned long currentTime = millis();
  
  // If we're playing, keep the display on
  if (isPlaying) {
    lastActivityTime = currentTime;
    if (!displayOn) {
      displayOn = true;
      display.display(); // Turn display back on
    }
    return;
  }
  
  // If we're not playing, check for timeout
  if (currentTime - lastActivityTime > DISPLAY_TIMEOUT) {
    if (displayOn) {
      displayOn = false;
      display.clearDisplay();
      display.display(); // Update display to clear it
    }
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
 * Updates the playlist with new JSON data and saves to SPIFFS
 */
void handlePostStreams() {
  // Get the JSON data from the request
  String jsonData = server.arg("plain");
  
  // Validate that we received data
  if (jsonData.length() == 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON data\"}");
    return;
  }
  
  // Validate JSON format (should be an array)
  jsonData.trim();
  if (!jsonData.startsWith("[") || !jsonData.endsWith("]")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON format - expected array\"}");
    return;
  }
  
  // Parse the JSON data
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, jsonData);
  
  if (error) {
    Serial.print("JSON parsing error: ");
    Serial.println(error.c_str());
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON format\"}");
    return;
  }
  
  // Ensure it's an array
  if (!doc.is<JsonArray>()) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON root must be an array\"}");
    return;
  }
  
  JsonArray array = doc.as<JsonArray>();
  
  // Validate array size
  if (array.size() > MAX_PLAYLIST_SIZE) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Playlist exceeds maximum size\"}");
    return;
  }
  
  // Clear existing playlist
  playlistCount = 0;
  
  // Process each item in the array
  for (JsonObject item : array) {
    if (playlistCount >= MAX_PLAYLIST_SIZE) {
      break;
    }
    
    // Validate required fields
    if (!item.containsKey("name") || !item.containsKey("url")) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Each item must have 'name' and 'url' fields\"}");
      return;
    }
    
    const char* name = item["name"];
    const char* url = item["url"];
    
    // Validate data
    if (!name || !url || strlen(name) == 0 || strlen(url) == 0) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Name and URL cannot be empty\"}");
      return;
    }
    
    // Validate URL format
    if (!VALIDATE_URL(url)) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid URL format\"}");
      return;
    }
    
    // Add to playlist
    strncpy(playlist[playlistCount].name, name, sizeof(playlist[playlistCount].name) - 1);
    playlist[playlistCount].name[sizeof(playlist[playlistCount].name) - 1] = '\0';
    strncpy(playlist[playlistCount].url, url, sizeof(playlist[playlistCount].url) - 1);
    playlist[playlistCount].url[sizeof(playlist[playlistCount].url) - 1] = '\0';
    playlistCount++;
  }
  
  // Save to SPIFFS
  savePlaylist();
  
  // Send success response as JSON
  String response = "{\"status\":\"success\",\"message\":\"Playlist updated successfully\"}";
  server.send(200, "application/json", response);
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
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }
    
    if (!doc.containsKey("url") || !doc.containsKey("name")) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameters: url and name\"}");
      return;
    }
    
    String url = doc["url"].as<String>();
    String name = doc["name"].as<String>();
    
    if (url.length() == 0 || name.length() == 0) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"URL and name cannot be empty\"}");
      return;
    }
    
    // Validate URL format
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid URL format. Must start with http:// or https://\"}");
      return;
    }
    
    startStream(url.c_str(), name.c_str());
    updateDisplay();
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stream started successfully\"}");
  } else if (server.hasArg("url") && server.hasArg("name")) {
    // Handle form data
    String url = server.arg("url");
    String name = server.arg("name");
    
    if (url.length() == 0 || name.length() == 0) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"URL and name cannot be empty\"}");
      return;
    }
    
    // Validate URL format
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid URL format. Must start with http:// or https://\"}");
      return;
    }
    
    startStream(url.c_str(), name.c_str());
    updateDisplay();
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stream stopped successfully\"}");
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
  updateDisplay();
  sendStatusToClients();  // Notify clients of status change
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stream stopped successfully\"}");
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
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }
    
    if (!doc.containsKey("volume")) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameter: volume\"}");
      return;
    }
    
    int newVolume = doc["volume"];
    
    if (newVolume < 0 || newVolume > 100) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Volume must be between 0 and 100\"}");
      return;
    }
    
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume * 21 / 100);  // ESP32-audioI2S uses 0-21 scale
    }
    updateDisplay();
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stream stopped successfully\"}");
  } else if (server.hasArg("volume")) {
    // Handle form data
    String vol = server.arg("volume");
    int newVolume = vol.toInt();
    
    if (newVolume < 0 || newVolume > 100) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Volume must be between 0 and 100\"}");
      return;
    }
    
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume * 21 / 100);  // ESP32-audioI2S uses 0-21 scale
    }
    updateDisplay();
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Volume set successfully\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameter: volume\"}");
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
  status += "\"streamTitle\":\"" + String(streamTitle) + "\",";
  status += "\"bitrate\":" + String(bitrate / 1000) + ",";
  status += "\"volume\":" + String(volume);
  status += "}";
  
  // Only broadcast if WebSocket server has clients
  if (webSocket.connectedClients() > 0) {
    webSocket.broadcastTXT(status);
  }
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
      {
        String status = "{";
        status += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
        status += "\"currentStream\":\"" + String(currentStream) + "\",";
        status += "\"currentStreamName\":\"" + String(currentStreamName) + "\",";
        status += "\"streamTitle\":\"" + String(streamTitle) + "\",";
        status += "\"bitrate\":" + String(bitrate / 1000) + ",";
        status += "\"volume\":" + String(volume);
        status += "}";
        webSocket.sendTXT(num, status);
      }
      break;
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client #%u disconnected\n", num);
      // Add a small delay to ensure proper cleanup
      delay(10);
      break;
    case WStype_TEXT:
      Serial.printf("WebSocket client #%u text: %s\n", num, payload);
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
  // Update last activity time
  lastActivityTime = millis();
  
  // If display is off, turn it on
  if (!displayOn) {
    displayOn = true;
  }
  
  display.clearDisplay();  // Clear the display
  
  if (isPlaying) {
    // Display when playing
    display.setTextSize(2);  // Larger font for status
    display.setCursor(0, 0);
    display.println("PLAYING");
    display.setTextSize(1);  // Normal font for stream info
    
    // Display station name (first line) with scrolling
    display.setCursor(0, 18);
    String stationName = String(currentStreamName);
    if (stationName.length() > 21) {  // ~21 chars fit on a 128px display
      static unsigned long lastScrollTime = 0;
      static int scrollOffset = 0;
      static String scrollText = "";
      
      // Reset scroll if text changed
      if (scrollText != stationName) {
        scrollText = stationName;
        scrollOffset = 0;
      }
      
      // Scroll every 500ms
      if (millis() - lastScrollTime > 500) {
        scrollOffset++;
        if (scrollOffset > (int)stationName.length()) {
          scrollOffset = 0;  // Reset scroll
        }
        lastScrollTime = millis();
      }
      
      // Display scrolled text
      String displayText = stationName + " *** ";
      if (scrollOffset < (int)displayText.length()) {
        displayText = displayText.substring(scrollOffset) + displayText.substring(0, scrollOffset);
      }
      display.println(displayText.substring(0, 21));
    } else {
      display.println(stationName);
    }
    
    // Display stream title (second line) if available with scrolling
    if (strlen(streamTitle) > 0) {
      display.setCursor(0, 30);
      // Scroll title if too long for display
      String title = String(streamTitle);
      if (title.length() > 21) {  // ~21 chars fit on a 128px display
        static unsigned long lastTitleScrollTime = 0;
        static int titleScrollOffset = 0;
        static String titleScrollText = "";
        
        // Reset scroll if text changed
        if (titleScrollText != title) {
          titleScrollText = title;
          titleScrollOffset = 0;
        }
        
        // Scroll every 500ms
        if (millis() - lastTitleScrollTime > 500) {
          titleScrollOffset++;
          if (titleScrollOffset > (int)title.length()) {
            titleScrollOffset = 0;  // Reset scroll
          }
          lastTitleScrollTime = millis();
        }
        
        // Display scrolled text
        String displayText = title + " *** ";
        if (titleScrollOffset < (int)displayText.length()) {
          displayText = displayText.substring(titleScrollOffset) + displayText.substring(0, titleScrollOffset);
        }
        display.println(displayText.substring(0, 21));
      } else {
        display.println(title);
      }
      
      // Display bitrate and volume on third line if available
      if (bitrate > 0) {
        display.setCursor(0, 42);
        display.print(bitrate / 1000);
        display.print(" kbps | ");
        display.print(volume);
        display.println("%");
      } else {
        display.setCursor(0, 42);
        display.print("Volume: ");
        display.print(volume);
        display.println("%");
      }
    } else {
      // Display bitrate and volume on second line if no title and bitrate available
      if (bitrate > 0) {
        display.setCursor(0, 30);
        display.print(bitrate / 1000);
        display.print(" kbps | ");
        display.print(volume);
        display.println("%");
      } else {
        display.setCursor(0, 30);
        display.print("Volume: ");
        display.print(volume);
        display.println("%");
      }
    }
    
    // Display IP address on the last line
    display.setCursor(0, 54);
    if (WiFi.status() == WL_CONNECTED) {
      display.println(WiFi.localIP().toString());
    } else {
      display.println("No IP");
    }
  } else {
    // Display when stopped
    display.setTextSize(2);  // Larger font for status
    display.setCursor(0, 0);
    display.println("STOPPED");
    display.setTextSize(1);  // Normal font for other text
    if (playlistCount > 0 && currentSelection < playlistCount) {
      // Show selected playlist item with scrolling
      display.setCursor(0, 18);
      String playlistName = String(playlist[currentSelection].name);
      if (playlistName.length() > 21) {  // ~21 chars fit on a 128px display
        static unsigned long lastPlaylistScrollTime = 0;
        static int playlistScrollOffset = 0;
        static String playlistScrollText = "";
        
        // Reset scroll if text changed
        if (playlistScrollText != playlistName) {
          playlistScrollText = playlistName;
          playlistScrollOffset = 0;
        }
        
        // Scroll every 500ms
        if (millis() - lastPlaylistScrollTime > 500) {
          playlistScrollOffset++;
          if (playlistScrollOffset > (int)playlistName.length()) {
            playlistScrollOffset = 0;  // Reset scroll
          }
          lastPlaylistScrollTime = millis();
        }
        
        // Display scrolled text
        String displayText = playlistName + " *** ";
        if (playlistScrollOffset < (int)displayText.length()) {
          displayText = displayText.substring(playlistScrollOffset) + displayText.substring(0, playlistScrollOffset);
        }
        display.println(displayText.substring(0, 21));
      } else {
        display.println(playlistName);
      }
    } else {
      // Show message when no streams available
      display.setCursor(0, 18);
      display.println("No streams");
    }
    
    // Display volume and IP on the last lines
    display.setCursor(0, 42);
    display.print("Volume: ");
    display.print(volume);
    display.println("%");
    
    // Display IP address on the last line
    display.setCursor(0, 54);
    if (WiFi.status() == WL_CONNECTED) {
      display.println(WiFi.localIP().toString());
    } else {
      display.println("No IP");
    }
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
        mpdClient.stop();  // Ensure previous client is properly closed
      }
      mpdClient = mpdServer.available();
      // Send MPD welcome message
      if (mpdClient && mpdClient.connected()) {
        mpdClient.print("OK MPD 0.20.0\n");
      }
    } else {
      // Reject connection if we already have a client
      WiFiClient rejectedClient = mpdServer.available();
      rejectedClient.stop();  // Properly close rejected connection
    }
  }
  
  // Check if client disconnected unexpectedly
  if (mpdClient && !mpdClient.connected()) {
    mpdClient.stop();
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
  return "ACK [0@0] {" + message + "}\n";
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
        currentSelection = index;
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
    mpdClient.print("volume: " + String(volume) + "\n");
    mpdClient.print("repeat: 0\n");
    mpdClient.print("random: 0\n");
    mpdClient.print("single: 0\n");
    mpdClient.print("consume: 0\n");
    mpdClient.print("playlist: 1\n");
    mpdClient.print("playlistlength: " + String(playlistCount) + "\n");
    mpdClient.print("mixrampdb: 0.000000\n");
    mpdClient.print("state: " + String(isPlaying ? "play" : "stop") + "\n");
    if (isPlaying && strlen(currentStreamName) > 0) {
      mpdClient.print("song: " + String(currentSelection) + "\n");
      mpdClient.print("songid: " + String(currentSelection) + "\n");
      mpdClient.print("time: 0:0\n");
      mpdClient.print("elapsed: 0.000\n");
      mpdClient.print("bitrate: " + String(bitrate / 1000) + "\n");
      mpdClient.print("audio: 44100:f:2\n");
      mpdClient.print("nextsong: " + String((currentSelection + 1) % playlistCount) + "\n");
      mpdClient.print("nextsongid: " + String((currentSelection + 1) % playlistCount) + "\n");
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("currentsong")) {
    // Current song command
    if (isPlaying && strlen(currentStreamName) > 0) {
      mpdClient.print("file: " + String(currentStream) + "\n");
      mpdClient.print("Title: " + String(currentStreamName) + "\n");
      mpdClient.print("Id: " + String(currentSelection) + "\n");
      mpdClient.print("Pos: " + String(currentSelection) + "\n");
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistinfo")) {
    // Playlist info command
    for (int i = 0; i < playlistCount; i++) {
      mpdClient.print("file: " + String(playlist[i].url) + "\n");
      mpdClient.print("Title: " + String(playlist[i].name) + "\n");
      mpdClient.print("Id: " + String(i) + "\n");
      mpdClient.print("Pos: " + String(i) + "\n");
      mpdClient.print("Last-Modified: 2025-01-01T00:00:00Z\n");
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistid")) {
    // Playlist ID command
    int id = -1;
    if (command.length() > 10) {
      id = command.substring(11).toInt();
    }
    
    if (id >= 0 && id < playlistCount) {
      mpdClient.print("file: " + String(playlist[id].url) + "\n");
      mpdClient.print("Title: " + String(playlist[id].name) + "\n");
      mpdClient.print("Id: " + String(id) + "\n");
      mpdClient.print("Pos: " + String(id) + "\n");
      mpdClient.print("Last-Modified: 2025-01-01T00:00:00Z\n");
    } else {
      // Return all if no specific ID
      for (int i = 0; i < playlistCount; i++) {
        mpdClient.print("file: " + String(playlist[i].url) + "\n");
        mpdClient.print("Title: " + String(playlist[i].name) + "\n");
        mpdClient.print("Id: " + String(i) + "\n");
        mpdClient.print("Pos: " + String(i) + "\n");
        mpdClient.print("Last-Modified: 2025-01-01T00:00:00Z\n");
      }
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("lsinfo")) {
    // List info command
    for (int i = 0; i < playlistCount; i++) {
      mpdClient.print("file: " + String(playlist[i].url) + "\n");
      mpdClient.print("Title: " + String(playlist[i].name) + "\n");
      mpdClient.print("Last-Modified: 2025-01-01T00:00:00Z\n");
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("setvol")) {
    // Set volume command
    if (command.length() > 7) {
      int newVolume = command.substring(7).toInt();
      if (newVolume >= 0 && newVolume <= 100) {
        volume = newVolume;
        if (audio) {
          audio->setVolume(volume * 21 / 100);  // ESP32-audioI2S uses 0-21 scale
        }
        updateDisplay();
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
    mpdClient.print("outputid: 0\n");
    mpdClient.print("outputname: I2S (External DAC)\n");
    mpdClient.print("outputenabled: 1\n");
    mpdClient.print("attribute: allowed_formats=\n");
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
    mpdClient.print("command: add\n");
    mpdClient.print("command: clear\n");
    mpdClient.print("command: currentsong\n");
    mpdClient.print("command: delete\n");
    mpdClient.print("command: disableoutput\n");
    mpdClient.print("command: enableoutput\n");
    mpdClient.print("command: load\n");
    mpdClient.print("command: lsinfo\n");
    mpdClient.print("command: next\n");
    mpdClient.print("command: outputs\n");
    mpdClient.print("command: pause\n");
    mpdClient.print("command: play\n");
    mpdClient.print("command: playlistid\n");
    mpdClient.print("command: playlistinfo\n");
    mpdClient.print("command: previous\n");
    mpdClient.print("command: save\n");
    mpdClient.print("command: setvol\n");
    mpdClient.print("command: status\n");
    mpdClient.print("command: stop\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("notcommands")) {
    // Not commands command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("tagtypes")) {
    // Tag types command
    mpdClient.print("tagtype: Artist\n");
    mpdClient.print("tagtype: Album\n");
    mpdClient.print("tagtype: Title\n");
    mpdClient.print("tagtype: Track\n");
    mpdClient.print("tagtype: Name\n");
    mpdClient.print("tagtype: Genre\n");
    mpdClient.print("tagtype: Date\n");
    mpdClient.print("tagtype: Composer\n");
    mpdClient.print("tagtype: Performer\n");
    mpdClient.print("tagtype: Comment\n");
    mpdClient.print("tagtype: Disc\n");
    mpdClient.print("tagtype: MUSICBRAINZ_ARTISTID\n");
    mpdClient.print("tagtype: MUSICBRAINZ_ALBUMID\n");
    mpdClient.print("tagtype: MUSICBRAINZ_ALBUMARTISTID\n");
    mpdClient.print("tagtype: MUSICBRAINZ_TRACKID\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("idle")) {
    // Idle command
    mpdClient.print("changed: playlist\n");
    mpdClient.print("changed: player\n");
    mpdClient.print("changed: mixer\n");
    mpdClient.print("changed: output\n");
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
