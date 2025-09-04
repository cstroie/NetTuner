/*
 * NetTuner - An ESP32-based internet radio player with MPD protocol support
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


#include "main.h"
#include "mpd.h"

// Spleen fonts https://www.onlinewebfonts.com/icon
#include "Spleen6x12.h" 
#include "Spleen8x16.h" 
#include "Spleen16x32.h" 


// MPD Interface instance
extern WiFiClient mpdClient;  // Declare the missing mpdClient
MPDInterface mpdInterface(mpdServer, streamTitle, streamName, streamURL, isPlaying, volume, bitrate, 
                          playlistCount, currentSelection, playlist, audio);

// Forward declarations
void sendStatusToClients();
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
      // Notify clients of stream title change
      sendStatusToClients();
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
    if (strcmp(streamName, info) != 0) {
      strncpy(streamName, info, sizeof(streamName) - 1);
      streamName[sizeof(streamName) - 1] = '\0';
      updateDisplay();
      // Notify clients of station name change
      sendStatusToClients();
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
    // Convert string to integer bitrate and convert to kbps (divide by 1000)
    int newBitrate = atoi(info) / 1000;
    // Update bitrate if it has changed
    if (newBitrate > 0 && newBitrate != bitrate) {
      bitrate = newBitrate;
      updateDisplay();
      // Notify clients of bitrate change
      sendStatusToClients();
    }
  }
}

/**
 * @brief Audio info callback function
 * This function is called by the Audio library when general audio information is available
 * @param info Pointer to the audio information
 */
void audio_info(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("Audio Info: ");
    Serial.println(info);
    
    // Check if the info contains StreamUrl=
    String infoStr = String(info);
    if (infoStr.startsWith("StreamUrl=")) {
      // Extract the URL part after "StreamUrl="
      String urlPart = infoStr.substring(10); // Skip "StreamUrl="
      
      // Remove quotes or double quotes if present
      if (urlPart.startsWith("\"") && urlPart.endsWith("\"") && urlPart.length() >= 2) {
        urlPart = urlPart.substring(1, urlPart.length() - 1);
      } else if (urlPart.startsWith("'") && urlPart.endsWith("'") && urlPart.length() >= 2) {
        urlPart = urlPart.substring(1, urlPart.length() - 1);
      }
      
      // Store the stream icon URL
      strncpy(streamIconURL, urlPart.c_str(), sizeof(streamIconURL) - 1);
      streamIconURL[sizeof(streamIconURL) - 1] = '\0';
      
      Serial.print("Stream Icon URL: ");
      Serial.println(streamIconURL);
      
      // Notify clients of the new stream icon
      sendStatusToClients();
    }
  }
}

/**
 * @brief Audio ICY URL callback function
 * This function is called by the Audio library when ICY URL information is available
 * @param info Pointer to the ICY URL information
 */
void audio_icyurl(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("ICY URL: ");
    Serial.println(info);
    // Store the ICY URL for later use
    strncpy(streamIcyURL, info, sizeof(streamIcyURL) - 1);
    streamIcyURL[sizeof(streamIcyURL) - 1] = '\0';
  }
}

/**
 * @brief Audio ICY description callback function
 * This function is called by the Audio library when ICY description information is available
 * @param info Pointer to the ICY description information
 */
void audio_icydescription(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("ICY Description: ");
    Serial.println(info);
  }
}

/**
 * @brief Audio ID3 data callback function
 * This function is called by the Audio library when ID3 data is available
 * @param info Pointer to the ID3 data
 */
void audio_id3data(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("ID3 Data: ");
    Serial.println(info);
  }
}

// Global variables definitions
char ssid[MAX_WIFI_NETWORKS][64] = {""};
char password[MAX_WIFI_NETWORKS][64] = {""};
int wifiNetworkCount = 0;
WebServer server(80);
WebSocketsServer webSocket(81);
WiFiServer mpdServer(6600);
char streamURL[256] = "";
char streamName[128] = "";
char streamTitle[128] = "";
char streamIcyURL[256] = "";
char streamIconURL[256] = "";
int bitrate = 0;
volatile bool isPlaying = false;
int volume = 11;
int bass = 0;
int midrange = 0;
int treble = 0;
unsigned long lastActivityTime = 0;
bool displayOn = true;
unsigned long startTime = 0;
unsigned long playStartTime = 0;
unsigned long totalPlayTime = 0;
const char* BUILD_TIME = __DATE__ "T" __TIME__"Z";
Audio *audio = nullptr;
bool audioConnected = false;
Config config = {
  DEFAULT_I2S_DOUT,
  DEFAULT_I2S_BCLK,
  DEFAULT_I2S_LRC,
  DEFAULT_LED_PIN,
  DEFAULT_ROTARY_CLK,
  DEFAULT_ROTARY_DT,
  DEFAULT_ROTARY_SW,
  DEFAULT_BOARD_BUTTON,
  DEFAULT_DISPLAY_SDA,
  DEFAULT_DISPLAY_SCL,
  DEFAULT_DISPLAY_WIDTH,
  DEFAULT_DISPLAY_HEIGHT,
  DEFAULT_DISPLAY_ADDR
};
Adafruit_SSD1306 display(config.display_width, config.display_height, &Wire, -1);
RotaryEncoder rotaryEncoder;
StreamInfo playlist[MAX_PLAYLIST_SIZE];
int playlistCount = 0;
int currentSelection = 0;
TaskHandle_t audioTaskHandle = NULL;

// Forward declarations for JSON helper functions
bool readJsonFile(const char* filename, size_t maxFileSize, DynamicJsonDocument& doc);
bool writeJsonFile(const char* filename, DynamicJsonDocument& doc);

// Player state tracking
struct PlayerState {
  bool playing = false;
  int volume = 11;
  int bass = 0;
  int midrange = 0;
  int treble = 0;
  int playlistIndex = 0;
  unsigned long lastSaveTime = 0;
  bool dirty = false;
};

PlayerState playerState;

/**
 * @brief Load player state from SPIFFS
 */
void loadPlayerState() {
  DynamicJsonDocument doc(512);
  if (readJsonFile("/player.json", 512, doc)) {
    playerState.playing = doc["playing"] | false;
    playerState.volume = doc["volume"] | 11;
    playerState.bass = doc["bass"] | 0;
    playerState.midrange = doc["midrange"] | 0;
    playerState.treble = doc["treble"] | 0;
    playerState.playlistIndex = doc["playlistIndex"] | 0;
    
    // Apply loaded state
    volume = playerState.volume;
    bass = playerState.bass;
    midrange = playerState.midrange;
    treble = playerState.treble;
    
    if (audio) {
      audio->setVolume(volume);
      audio->setTone(bass, midrange, treble);
    }
    
    if (playerState.playlistIndex >= 0 && playerState.playlistIndex < playlistCount) {
      currentSelection = playerState.playlistIndex;
    }
    
    Serial.println("Loaded player state from SPIFFS");
    
    // If was playing, resume playback
    if (playerState.playing && playlistCount > 0 && currentSelection < playlistCount) {
      Serial.println("Resuming playback from saved state");
      startStream(playlist[currentSelection].url, playlist[currentSelection].name);
    }
  } else {
    Serial.println("No player state file found, using defaults");
  }
}

/**
 * @brief Save player state to SPIFFS
 */
void savePlayerState() {
  DynamicJsonDocument doc(512);
  doc["playing"] = isPlaying;
  doc["volume"] = volume;
  doc["bass"] = bass;
  doc["midrange"] = midrange;
  doc["treble"] = treble;
  doc["playlistIndex"] = currentSelection;
  
  if (writeJsonFile("/player.json", doc)) {
    Serial.println("Saved player state to SPIFFS");
    playerState.dirty = false;
  } else {
    Serial.println("Failed to save player state to SPIFFS");
  }
}

/**
 * @brief Mark player state as dirty (needs saving)
 */
void markPlayerStateDirty() {
  playerState.dirty = true;
}

/**
 * @brief Arduino setup function
 * Initializes all system components including WiFi, audio, display, and servers
 * This function is called once at startup to configure the hardware and software components.
 */
void setup() {
  Serial.begin(115200);
  // Initialize start time for uptime tracking
  startTime = millis() / 1000;  // Store in seconds
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
    
  // Load configuration
  loadConfig();
  
  // Initialize LED pin
  pinMode(config.led_pin, OUTPUT);
  digitalWrite(config.led_pin, LOW);  // Turn off LED initially
  
 // Initialize board button with pull-up resistor
  pinMode(config.board_button, INPUT_PULLUP);

  // Initialize OLED display
  // Configure I2C pins
  Wire.begin(config.display_sda, config.display_scl);
  display.begin(SSD1306_SWITCHCAPVCC, config.display_address);
  display.clearDisplay();
  display.setFont(&Spleen8x16);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(32, 12);
  display.print("NetTuner");
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
        display.clearDisplay();
        display.setCursor(32, 12);
        display.print("NetTuner");
        display.setCursor(0, 30);
        display.print("WiFi connecting");
        display.setCursor(0, 45);
        display.print(String(ssid[i]));
        display.display();
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
  }
  
  if (connected) {
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP().toString());
    display.clearDisplay();
    display.setCursor(32, 12);
    display.print("NetTuner");
    display.setCursor(0, 30);
    display.print(String(WiFi.SSID()));
    display.setCursor(0, 62);
    display.print(WiFi.localIP().toString());
    display.display();
  } else {
    Serial.println("Failed to connect to any configured WiFi network or no WiFi configured");
    display.clearDisplay();
    display.setCursor(32, 12);
    display.print("NetTuner");
    display.setCursor(0, 30);
    display.print("Starting AP Mode");
    display.display();
    
    // Start WiFi access point mode with error handling
    if (WiFi.softAP("NetTuner-Setup")) {
      Serial.println("Access Point Started");
      Serial.print("AP IP Address: ");
      Serial.println(WiFi.softAPIP().toString());
      display.setCursor(0, 62);
      display.print(WiFi.softAPIP().toString());
      display.display();
    } else {
      Serial.println("Failed to start Access Point");
      display.setCursor(0, 62);
      display.print("AP Start Failed");
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
  
  // Load player state
  loadPlayerState();
  
  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/playlist", HTTP_GET, handlePlaylistPage);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/wifi", HTTP_GET, handleWiFiConfig);
  server.on("/about", HTTP_GET, handleAboutPage);
  server.on("/api/streams", HTTP_GET, handleGetStreams);
  server.on("/api/streams", HTTP_POST, handlePostStreams);
  server.on("/api/play", HTTP_POST, handlePlay);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/volume", HTTP_POST, handleVolume);
  server.on("/api/tone", HTTP_POST, handleTone);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/config/export", HTTP_GET, handleExportConfig);
  server.on("/api/config/import", HTTP_POST, handleImportConfig);
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi/save", HTTP_POST, handleWiFiSave);
  server.on("/api/wifi/status", HTTP_GET, handleWiFiStatus);
  server.on("/api/wifi/config", HTTP_GET, handleWiFiConfigAPI);
  server.on("/w", HTTP_GET, handleSimpleWebPage);
  server.on("/w", HTTP_POST, handleSimpleWebPage);
   
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
 * @param pvParameters Task parameters (not used)
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
 * @brief Handle board button input
 * Processes the built-in button (GPIO 0) for play/stop toggle functionality
 * This function implements debouncing and toggles playback state when pressed.
 */
void handleBoardButton() {
  static bool lastButtonState = HIGH;  // Keep track of button state
  static unsigned long lastDebounceTime = 0;  // Last time the button was pressed
  const unsigned long debounceDelay = 50;  // Debounce time in milliseconds
  static bool buttonPressHandled = false;  // Track if we've handled this press
  
  // Handle board button press for play/stop toggle
  int buttonReading = digitalRead(config.board_button);
  
  // Check if button state changed (debounce)
  if (buttonReading != lastButtonState) {
    lastDebounceTime = millis();
    buttonPressHandled = false;  // Reset handled flag when state changes
  }
  
  // If button state has been stable for debounce delay
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // If button is pressed (LOW due to pull-up) and we haven't handled this press yet
    if (buttonReading == LOW && lastButtonState == HIGH && !buttonPressHandled) {
      // Toggle play/stop
      if (isPlaying) {
        stopStream();
        markPlayerStateDirty();
      } else {
        // If we have a current stream, resume it
        if (strlen(streamURL) > 0) {
          startStream();
        } 
        // Otherwise, if we have playlist items, play the selected one
        else if (playlistCount > 0 && currentSelection < playlistCount) {
          startStream(playlist[currentSelection].url, playlist[currentSelection].name);
        }
        markPlayerStateDirty();
      }
      updateDisplay();
      sendStatusToClients();
      buttonPressHandled = true;  // Mark this press as handled
    }
    // If button is released, reset handled flag
    else if (buttonReading == HIGH && lastButtonState == LOW) {
      buttonPressHandled = false;
    }
  }
  
  lastButtonState = buttonReading;
}

/**
 * @brief Arduino main loop function
 * Handles web server requests, WebSocket events, rotary encoder input, and MPD commands
 * This is the main application loop that runs continuously after setup()
 */
void loop() {
  static unsigned long streamStoppedTime = 0;
  handleRotary();          // Process rotary encoder input
  server.handleClient();   // Process incoming web requests
  webSocket.loop();        // Process WebSocket events
  mpdInterface.handleClient();       // Process MPD commands
  handleBoardButton();     // Process board button input
  
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
        if (strlen(streamURL) > 0) {
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
        int newBitrate = audio->getBitRate() / 1000;  // Convert bps to kbps
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
            display.setCursor(32, 12);
            display.print("NetTuner");
            display.setCursor(0, 30);
            display.print("WiFi Reconnect");
            display.setCursor(0, 62);
            display.print(WiFi.localIP().toString());
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
  delay(100);
}

/**
 * @brief Handle WiFi configuration page
 * Serves the WiFi configuration page
 * This function reads the wifi.html file from SPIFFS and sends it to the client
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
 * This function provides the list of configured WiFi networks in JSON format
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
 * This function scans for available WiFi networks and returns them along with
 * the list of already configured networks
 */
void handleWiFiScan() {
  int n = WiFi.scanNetworks();
  String json = "{";
  // Add available networks
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
  // Send the JSON response
  server.send(200, "application/json", json);
}

/**
 * @brief Handle WiFi configuration save
 * Saves WiFi credentials to SPIFFS
 * This function receives WiFi credentials via HTTP POST and saves them to wifi.json
 * It supports both single network and multiple network configurations
 */
void handleWiFiSave() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON data\"}");
    return;
  }
  
  String json = server.arg("plain");
  DynamicJsonDocument doc(2048);  // Increased size for array format
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  
  // Handle the new JSON array format [{"ssid": "name", "password": "pass"}, ...]
  wifiNetworkCount = 0;
  
  if (doc.is<JsonArray>()) {
    JsonArray networks = doc.as<JsonArray>();
    
    for (JsonObject network : networks) {
      if (wifiNetworkCount >= MAX_WIFI_NETWORKS) break;
      
      if (network.containsKey("ssid")) {
        const char* ssidValue = network["ssid"];
        if (ssidValue && strlen(ssidValue) > 0 && strlen(ssidValue) < sizeof(ssid[wifiNetworkCount])) {
          strncpy(ssid[wifiNetworkCount], ssidValue, sizeof(ssid[wifiNetworkCount]) - 1);
          ssid[wifiNetworkCount][sizeof(ssid[wifiNetworkCount]) - 1] = '\0';
        } else {
          server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid SSID\"}");
          return;
        }
        
        if (network.containsKey("password")) {
          const char* pwdValue = network["password"];
          if (pwdValue && strlen(pwdValue) < sizeof(password[wifiNetworkCount])) {
            strncpy(password[wifiNetworkCount], pwdValue, sizeof(password[wifiNetworkCount]) - 1);
            password[wifiNetworkCount][sizeof(password[wifiNetworkCount]) - 1] = '\0';
          } else {
            password[wifiNetworkCount][0] = '\0';
          }
        } else {
          password[wifiNetworkCount][0] = '\0';
        }
        
        wifiNetworkCount++;
      }
    }
  };
  
  saveWiFiCredentials();
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"WiFi configuration saved\"}");
}

/**
 * @brief Handle WiFi status request
 * Returns the current WiFi connection status as JSON
 * This function provides information about the current WiFi connection including
 * connection status, SSID, IP address, and signal strength
 */
void handleWiFiStatus() {
  String json = "{";
  // Add connection status
  if (WiFi.status() == WL_CONNECTED) {
    json += "\"connected\":true,";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
  } else {
    json += "\"connected\":false";
  }
  json += "}";
  // Send the JSON response
  server.send(200, "application/json", json);
}

/**
 * @brief Read JSON file from SPIFFS
 * Helper function to read and parse JSON files from SPIFFS
 * @param filename Path to the file in SPIFFS
 * @param maxFileSize Maximum allowed file size
 * @param doc JsonDocument to populate with parsed data
 * @return true if successful, false otherwise
 */
bool readJsonFile(const char* filename, size_t maxFileSize, DynamicJsonDocument& doc) {
  // Check if the file exists
  if (!SPIFFS.exists(filename)) {
    Serial.printf("JSON file not found: %s\n", filename);
    return false;
  }
  
  // Open the file
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.printf("Failed to open JSON file: %s\n", filename);
    return false;
  }
  
  // Get the size of the file
  size_t size = file.size();
  if (size > maxFileSize) {
    Serial.printf("JSON file too large: %s\n", filename);
    file.close();
    return false;
  }
  
  // Check if the file is empty
  if (size == 0) {
    Serial.printf("JSON file is empty: %s\n", filename);
    file.close();
    return false;
  }
  
  // Allocate buffer for file content
  std::unique_ptr<char[]> buf(new char[size + 1]);
  if (!buf) {
    Serial.printf("Error: Failed to allocate memory for JSON file: %s\n", filename);
    file.close();
    return false;
  }
  
  // Read the file content
  if (file.readBytes(buf.get(), size) != size) {
    Serial.printf("Failed to read JSON file: %s\n", filename);
    file.close();
    return false;
  }
  
  buf[size] = '\0';
  file.close();
  
  // Parse the JSON document
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.printf("Failed to parse JSON file %s: %s\n", filename, error.c_str());
    return false;
  }
  
  return true;
}

/**
 * @brief Write JSON file to SPIFFS
 * Helper function to serialize and write JSON files to SPIFFS
 * @param filename Path to the file in SPIFFS
 * @param doc JsonDocument to serialize
 * @return true if successful, false otherwise
 */
bool writeJsonFile(const char* filename, DynamicJsonDocument& doc) {
  // Create backup of existing file
  String backupFilename = String(filename) + ".bak";
  if (SPIFFS.exists(filename)) {
    if (SPIFFS.exists(backupFilename)) {
      SPIFFS.remove(backupFilename);
    }
    if (!SPIFFS.rename(filename, backupFilename)) {
      Serial.printf("Warning: Failed to create backup of %s\n", filename);
    }
  }
  
  // Open the file for writing
  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.printf("Failed to open JSON file for writing: %s\n", filename);
    // Try to restore from backup
    if (SPIFFS.exists(backupFilename)) {
      if (SPIFFS.rename(backupFilename, filename)) {
        Serial.printf("Restored %s from backup\n", filename);
      } else {
        Serial.printf("Error: Failed to restore %s from backup\n", filename);
      }
    }
    return false;
  }
  
  // Serialize the JSON document to the file
  size_t bytesWritten = serializeJson(doc, file);
  if (bytesWritten == 0) {
    Serial.printf("Failed to write JSON to file: %s\n", filename);
    file.close();
    // Try to restore from backup
    if (SPIFFS.exists(backupFilename)) {
      SPIFFS.remove(filename); // Remove the failed file
      if (SPIFFS.rename(backupFilename, filename)) {
        Serial.printf("Restored %s from backup\n", filename);
      } else {
        Serial.printf("Error: Failed to restore %s from backup\n", filename);
      }
    }
    return false;
  }
  
  file.close();
  
  // Remove backup file after successful save
  if (SPIFFS.exists(backupFilename)) {
    SPIFFS.remove(backupFilename);
  }
  
  return true;
}

/**
 * @brief Load WiFi credentials from SPIFFS
 * This function reads WiFi credentials from wifi.json in SPIFFS and populates
 * the ssid and password arrays. It supports the new JSON array format.
 */
void loadWiFiCredentials() {
  // Parse the JSON document
  DynamicJsonDocument doc(2048);  // Increased size for array format
  if (!readJsonFile("/wifi.json", 2048, doc)) {
    return;
  }
  // Handle the JSON array format [{"ssid": "name", "password": "pass"}, ...]
  if (doc.is<JsonArray>()) {
    JsonArray networks = doc.as<JsonArray>();
    wifiNetworkCount = 0;
    // Iterate through each network object
    for (JsonObject network : networks) {
      if (wifiNetworkCount >= MAX_WIFI_NETWORKS) break;
      // Check if SSID exists
      if (network.containsKey("ssid")) {
        const char* ssidValue = network["ssid"];
        if (ssidValue) {
          strncpy(ssid[wifiNetworkCount], ssidValue, sizeof(ssid[wifiNetworkCount]) - 1);
          ssid[wifiNetworkCount][sizeof(ssid[wifiNetworkCount]) - 1] = '\0';
        } else {
          ssid[wifiNetworkCount][0] = '\0';
        }
        // Check if password exists
        if (network.containsKey("password")) {
          const char* pwdValue = network["password"];
          if (pwdValue) {
            strncpy(password[wifiNetworkCount], pwdValue, sizeof(password[wifiNetworkCount]) - 1);
            password[wifiNetworkCount][sizeof(password[wifiNetworkCount]) - 1] = '\0';
          } else {
            password[wifiNetworkCount][0] = '\0';
          }
        } else {
          password[wifiNetworkCount][0] = '\0';
        }
        // Increment the network count
        wifiNetworkCount++;
      }
    }
  }
  // Print loaded WiFi credentials
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
 * This function saves the current WiFi credentials to wifi.json in SPIFFS.
 * It stores networks in the new JSON array format.
 */
void saveWiFiCredentials() {
  DynamicJsonDocument doc(2048); // Increased size for array format
  JsonArray networks = doc.to<JsonArray>();
  // Save networks in the JSON array format [{"ssid": "name", "password": "pass"}, ...]
  for (int i = 0; i < wifiNetworkCount; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = ssid[i];
    if (strlen(password[i]) > 0) {
      network["password"] = password[i];
    }
  }
  
  // Save the JSON document to SPIFFS using helper function
  if (writeJsonFile("/wifi.json", doc)) {
    Serial.println("Saved WiFi credentials to SPIFFS");
  } else {
    Serial.println("Failed to save WiFi credentials to SPIFFS");
  }
}

/**
 * @brief Load configuration from SPIFFS
 * This function reads configuration from config.json in SPIFFS
 */
void loadConfig() {
  // Parse the JSON document
  DynamicJsonDocument doc(1024);
  if (!readJsonFile("/config.json", 1024, doc)) {
    Serial.println("Config file not found, using defaults");
    // Initialize config with default values
    config.i2s_dout = DEFAULT_I2S_DOUT;
    config.i2s_bclk = DEFAULT_I2S_BCLK;
    config.i2s_lrc = DEFAULT_I2S_LRC;
    config.led_pin = DEFAULT_LED_PIN;
    config.rotary_clk = DEFAULT_ROTARY_CLK;
    config.rotary_dt = DEFAULT_ROTARY_DT;
    config.rotary_sw = DEFAULT_ROTARY_SW;
    config.board_button = DEFAULT_BOARD_BUTTON;
    config.display_sda = DEFAULT_DISPLAY_SDA;
    config.display_scl = DEFAULT_DISPLAY_SCL;
    config.display_width = DEFAULT_DISPLAY_WIDTH;
    config.display_height = DEFAULT_DISPLAY_HEIGHT;
    config.display_address = DEFAULT_DISPLAY_ADDR;
    // Save the default configuration to file
    saveConfig();
    return;
  }
  // Load configuration values, using defaults for missing values
  config.i2s_dout = doc.containsKey("i2s_dout") ? doc["i2s_dout"] : DEFAULT_I2S_DOUT;
  config.i2s_bclk = doc.containsKey("i2s_bclk") ? doc["i2s_bclk"] : DEFAULT_I2S_BCLK;
  config.i2s_lrc = doc.containsKey("i2s_lrc") ? doc["i2s_lrc"] : DEFAULT_I2S_LRC;
  config.led_pin = doc.containsKey("led_pin") ? doc["led_pin"] : DEFAULT_LED_PIN;
  config.rotary_clk = doc.containsKey("rotary_clk") ? doc["rotary_clk"] : DEFAULT_ROTARY_CLK;
  config.rotary_dt = doc.containsKey("rotary_dt") ? doc["rotary_dt"] : DEFAULT_ROTARY_DT;
  config.rotary_sw = doc.containsKey("rotary_sw") ? doc["rotary_sw"] : DEFAULT_ROTARY_SW;
  config.board_button = doc.containsKey("board_button") ? doc["board_button"] : DEFAULT_BOARD_BUTTON;
  config.display_sda = doc.containsKey("display_sda") ? doc["display_sda"] : DEFAULT_DISPLAY_SDA;
  config.display_scl = doc.containsKey("display_scl") ? doc["display_scl"] : DEFAULT_DISPLAY_SCL;
  config.display_width = doc.containsKey("display_width") ? doc["display_width"] : DEFAULT_DISPLAY_WIDTH;
  config.display_height = doc.containsKey("display_height") ? doc["display_height"] : DEFAULT_DISPLAY_HEIGHT;
  config.display_address = doc.containsKey("display_address") ? doc["display_address"] : DEFAULT_DISPLAY_ADDR;
  // Print loaded configuration
  Serial.println("Loaded configuration from SPIFFS");
}

/**
 * @brief Save configuration to SPIFFS
 * This function saves the current configuration to config.json in SPIFFS
 */
void saveConfig() {
  // Create a JSON document
  DynamicJsonDocument doc(1024);
  doc["i2s_dout"] = config.i2s_dout;
  doc["i2s_bclk"] = config.i2s_bclk;
  doc["i2s_lrc"] = config.i2s_lrc;
  doc["led_pin"] = config.led_pin;
  doc["rotary_clk"] = config.rotary_clk;
  doc["rotary_dt"] = config.rotary_dt;
  doc["rotary_sw"] = config.rotary_sw;
  doc["board_button"] = config.board_button;
  doc["display_sda"] = config.display_sda;
  doc["display_scl"] = config.display_scl;
  doc["display_width"] = config.display_width;
  doc["display_height"] = config.display_height;
  doc["display_address"] = config.display_address;
  
  // Save the JSON document to SPIFFS using helper function
  if (writeJsonFile("/config.json", doc)) {
    Serial.println("Saved configuration to SPIFFS");
  } else {
    Serial.println("Failed to save configuration to SPIFFS");
  }
}


/**
 * @brief Initialize audio output interface
 * Configures the selected audio output method
 * This function initializes the ESP32-audioI2S library with I2S pin configuration
 * and sets up the audio buffer with an increased size for better performance.
 */
void setupAudioOutput() {
  // Initialize ESP32-audioI2S
  audio = new Audio(false); // false = use I2S, true = use DAC
  audio->setPinout(config.i2s_bclk, config.i2s_lrc, config.i2s_dout);
  audio->setVolume(volume); // Use 0-22 scale directly
  audio->setBufsize(65536, 0); // Increased buffer size to 64KB for better streaming performance
}

/**
 * @brief Start streaming an audio stream
 * Stops any currently playing stream and begins playing a new one
 * If called without parameters, resumes playback of streamURL if available
 * @param url URL of the audio stream to play (optional)
 * @param name Human-readable name of the stream (optional)
 */
void startStream(const char* url, const char* name) {
  bool resume = false;
  // Stop the currently playing stream if the stream changes
  if (audio && url && strlen(url) > 0) {
    // Stop first
    stopStream();
  }
  // If no URL provided, check if we have a current stream to resume
  if (!url || strlen(url) == 0) {
    if (strlen(streamURL) > 0) {
      // Resume playback of current stream
      url = streamURL;
      // Use current name if available, otherwise use a default
      if (!name || strlen(name) == 0) {
        name = (strlen(streamName) > 0) ? streamName : "Unknown Station";
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
  // Check for empty strings
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
    strncpy(streamURL, url, sizeof(streamURL) - 1);
    streamURL[sizeof(streamURL) - 1] = '\0';
    strncpy(streamName, name, sizeof(streamName) - 1);
    streamName[sizeof(streamName) - 1] = '\0';
  };
  // Set playback status to playing
  isPlaying = true;
  // Track play time
  playStartTime = millis() / 1000;  // Store in seconds
  // Turn on LED when playing
  digitalWrite(config.led_pin, HIGH);
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
 * This function stops audio playback, clears stream information, and resets
 * the playback state to stopped.
 */
void stopStream() {
  // Stop the audio playback
  if (audio) {
    audio->stopSong();
  }
  audioConnected = false;
  isPlaying = false;             // Set playback status to stopped
  streamURL[0] = '\0';       // Clear current stream URL
  streamName[0] = '\0';   // Clear current stream name
  streamTitle[0] = '\0';         // Clear stream title
  streamIcyURL[0] = '\0';        // Clear ICY URL
  streamIconURL[0] = '\0';       // Clear stream icon URL
  bitrate = 0;                   // Clear bitrate
  // Update total play time when stopping
  if (playStartTime > 0) {
    totalPlayTime += (millis() / 1000) - playStartTime;
    playStartTime = 0;
  }
  // Turn off LED when stopped
  digitalWrite(config.led_pin, LOW);
  updateDisplay();  // Refresh the display
  sendStatusToClients();  // Notify clients of status change
}

/**
 * @brief Load playlist from SPIFFS storage
 * Reads playlist.json from SPIFFS and populates the playlist array
 * This function loads the playlist from SPIFFS with error recovery mechanisms.
 * If the playlist file is corrupted, it creates a backup and a new empty playlist.
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
  // Read the file content into the buffer
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
  // Check for JSON parsing errors
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
  // Check if the JSON document is an array
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
  // Populate the playlist array
  JsonArray array = doc.as<JsonArray>();
  playlistCount = 0;
  // Iterate through the JSON array
  for (JsonObject item : array) {
    if (playlistCount >= 20) {
      Serial.println("Warning: Playlist limit reached (20 entries)");
      break;
    }
    // Check if the item has the required keys
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
  // Check if any valid streams were loaded
  if (playlistCount == 0) {
    Serial.println("Error: No valid streams found in playlist");
  } else {
    Serial.print("Loaded ");
    Serial.print(playlistCount);
    Serial.println(" streams from playlist");
  }
}

/**
 * @brief Save playlist to SPIFFS storage
 * Serializes the current playlist array to playlist.json
 * This function saves the current playlist to SPIFFS with backup functionality.
 * It creates a backup before saving and restores from backup if saving fails.
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
    // Create JSON object for the playlist entry
    JsonObject item = array.createNestedObject();
    item["name"] = playlist[i].name;
    item["url"] = playlist[i].url;
  }
  
  // Save the JSON document to SPIFFS using helper function
  if (writeJsonFile("/playlist.json", doc)) {
    Serial.println("Saved playlist to file");
  } else {
    Serial.println("Failed to save playlist to file");
  }
}


/**
 * @brief Interrupt service routine for rotary encoder
 * Handles rotary encoder rotation events
 */
void rotaryISR() {
  rotaryEncoder.handleRotation();
}

/**
 * @brief Initialize rotary encoder hardware
 * Configures pins and attaches interrupt handlers for the rotary encoder
 * This function sets up the rotary encoder pins with internal pull-up resistors
 * and attaches interrupt handlers for rotation and button press events.
 */
void setupRotaryEncoder() {
  // Configure rotary encoder pins with internal pull-up resistors
  pinMode(config.rotary_clk, INPUT_PULLUP);   // Enable internal pull-up resistor
  pinMode(config.rotary_dt, INPUT_PULLUP);    // Enable internal pull-up resistor
  pinMode(config.rotary_sw, INPUT_PULLUP);    // Enable internal pull-up resistor
  // Attach interrupt handler for rotary encoder rotation
  attachInterrupt(digitalPinToInterrupt(config.rotary_clk), rotaryISR, CHANGE);
  // Attach interrupt handler for rotary encoder button press
  attachInterrupt(digitalPinToInterrupt(config.rotary_sw), []() {
    rotaryEncoder.handleButtonPress();
  }, FALLING);
}


/**
 * @brief Handle rotary encoder input
 * Processes rotation and button press events from the rotary encoder
 * This function processes rotary encoder events and controls volume when playing
 * or playlist selection when stopped. It also handles button presses to start/stop playback.
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
        // If playing, increase volume by 1 (capped at 22)
        volume = min(22, volume + 1);
        if (audio) {
          audio->setVolume(volume);  // ESP32-audioI2S uses 0-22 scale
        }
        markPlayerStateDirty();
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
        // If playing, decrease volume by 1 (capped at 0)
        volume = max(0, volume - 1);
        if (audio) {
          audio->setVolume(volume);  // ESP32-audioI2S uses 0-22 scale
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
    lastActivityTime = millis(); // Update activity time on user interaction
    if (!displayOn) {
      displayOn = true;
    }
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
      if (!isPlaying) {
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
 * This function manages the OLED display timeout to conserve power. When not playing,
 * the display turns off after 30 seconds of inactivity. The display turns back on
 * when there's activity or when playback starts.
 * 
 * When playing, the display stays on but activity time is updated periodically
 * to prevent immediate timeout after playback stops.
 */
void handleDisplayTimeout() {
  const unsigned long DISPLAY_TIMEOUT = 30000; // 30 seconds
  unsigned long currentTime = millis();
  
  // Handle potential millis() overflow
  if (currentTime < lastActivityTime) {
    lastActivityTime = currentTime; // Reset on overflow
  }
  
  // If we're playing, keep the display on
  if (isPlaying) {
    // Update activity time periodically during playback to prevent timeout
    static unsigned long lastPlaybackActivityUpdate = 0;
    if (currentTime - lastPlaybackActivityUpdate > 5000) { // Every 5 seconds
      lastActivityTime = currentTime;
      lastPlaybackActivityUpdate = currentTime;
    }
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
 * This function reads the index.html file from SPIFFS and sends it to the client
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
 * @brief Handle simple web page request
 * Serves a minimal HTML page for controlling the radio
 * This function provides a simple interface with play/stop controls
 * and stream selection without CSS or JavaScript
 */
void handleSimpleWebPage() {
  if (server.method() == HTTP_POST) {
    // Handle form submission
    if (server.hasArg("action")) {
      String action = server.arg("action");
      // Perform action based on form input
      if (action == "play") {
        // Play selected stream
        if (server.hasArg("stream") && playlistCount > 0) {
          int streamIndex = server.arg("stream").toInt();
          if (streamIndex >= 0 && streamIndex < playlistCount) {
            currentSelection = streamIndex;
            startStream(playlist[streamIndex].url, playlist[streamIndex].name);
          }
        } else if (strlen(streamURL) > 0) {
          // Resume current stream
          startStream();
        } else if (playlistCount > 0 && currentSelection < playlistCount) {
          // Play currently selected stream
          startStream(playlist[currentSelection].url, playlist[currentSelection].name);
        }
      } else if (action == "stop") {
        // Stop playback
        stopStream();
      }
    }
  }
  // Serve the HTML page
  String html = "<!DOCTYPE html><html>";
  html += "<head><title>NetTuner</title><link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@picocss/pico@2/css/pico.classless.min.css\"></head><body>";
  html += "<header><h1>NetTuner</h1></header>";
  html += "<main>";
  // Show current status
  html += "<section>";
  html += "<h2>Status</h2>";
  html += "<p><b>Status:</b> ";
  html += isPlaying ? "Playing" : "Stopped";
  html += "</p>";
  // Show current stream name
  if (isPlaying && streamName[0]) {
    html += "<p><b>Current Stream:</b> ";
    html += streamName;
    html += "</p>";
  } else if (!isPlaying && playlistCount > 0 && currentSelection < playlistCount) {
    html += "<p><b>Selected Stream:</b> ";
    html += playlist[currentSelection].name;
    html += "</p>";
  }
  html += "</section>";
  // Play/Stop buttons
  html += "<section>";
  html += "<h2>Controls</h2>";
  html += "<form method='post'>";
  html += "<button name='action' value='play' type='submit'>Play</button> ";
  html += "<button name='action' value='stop' type='submit'>Stop</button>";
  html += "</form>";
  html += "</section>";
  // Stream selection
  html += "<section>";
  html += "<h2>Playlist</h2>";
  if (playlistCount > 0) {
    html += "<form method='post'>";
    html += "<label for='stream'>Select Stream:</label>";
    html += "<select name='stream' id='stream'>";
    // Populate the dropdown with available streams
    for (int i = 0; i < playlistCount; i++) {
      html += "<option value='" + String(i) + "'";
      if (i == currentSelection) {
        html += " selected";
      }
      html += ">" + String(playlist[i].name) + "</option>";
    }
    html += "</select>";
    html += "<button name='action' value='play' type='submit'>Play Selected</button>";
    html += "</form>";
  } else {
    html += "<p>No streams available</p>";
  }
  html += "</section>";
  html += "</main>";
  html += "<footer><p>NetTuner Simple Interface</p></footer>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

/**
 * @brief Handle playlist page request
 * Serves the playlist.html file
 * This function reads the playlist.html file from SPIFFS and sends it to the client
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
 * @brief Handle configuration page request
 * Serves the config.html file
 * This function reads the config.html file from SPIFFS and sends it to the client
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
 * @brief Handle about page request
 * Serves the about.html file
 * This function reads the about.html file from SPIFFS and sends it to the client
 */
void handleAboutPage() {
  File file = SPIFFS.open("/about.html", "r");
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
 * This function serves the current playlist in JSON format. If the playlist file
 * doesn't exist, it creates a default empty one.
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
  
  // Stream the file contents
  server.streamFile(file, "application/json");
  file.close();
}

/**
 * @brief Handle POST request for streams
 * Updates the playlist with new JSON data and saves to SPIFFS
 * This function receives a new playlist via HTTP POST, validates it, and saves it to SPIFFS.
 * It supports both JSON array format and validates each stream entry.
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
  // Check for JSON parsing errors
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
  // Get the array
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
    // Extract name and url
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
 * This function handles HTTP requests to start playing a stream. It supports both
 * JSON payload and form data, validates the input, and starts the stream.
 */
void handlePlay() {
  String url, name;
  int index = -1;
  // Check for JSON payload
  if (server.hasArg("plain")) {
    // Handle JSON payload
    String json = server.arg("plain");
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json);
    // Check for JSON parsing errors
    if (error) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }
    // Check for required parameters
    if (!doc.containsKey("url") || !doc.containsKey("name")) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameters: url and name\"}");
      return;
    }
    // Extract URL and name
    url = doc["url"].as<String>();
    name = doc["name"].as<String>();
    index = doc["index"].as<int>();
  } else if (server.hasArg("url") && server.hasArg("name")) {
    // Handle form data for the simple web page
    url = server.arg("url");
    name = server.arg("name");
  } else {
    server.send(400, "text/plain", "Missing required parameters: url and name");
    return;
  }
  // Validate extracted values
  if (url.length() == 0 || name.length() == 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"URL and name cannot be empty\"}");
    return;
  }
  // Validate URL format
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid URL format. Must start with http:// or https://\"}");
    return;
  }
  // Update currentSelection based on index
  if (index > 0) {
    // If index is valid, update currentSelection
    currentSelection = index - 1;
  } else {
    // Find the stream in the playlist and update currentSelection
    for (int i = 0; i < playlistCount; i++) {
      if (strcmp(playlist[i].url, url.c_str()) == 0) {
        currentSelection = i;
        break;
      }
    }
  }
  // Start the stream
  startStream(url.c_str(), name.c_str());
  // Save player state when user requests to play
  markPlayerStateDirty();
  savePlayerState();
  // Update display and notify clients
  updateDisplay();
  sendStatusToClients();
  // Send success response
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stream started successfully\"}");
}

/**
 * @brief Handle stop request
 * Stops the currently playing stream
 * This function handles HTTP requests to stop the currently playing stream.
 */
void handleStop() {
  // Stop any currently playing stream
  stopStream();
  // Save player state when user stops the stream
  markPlayerStateDirty();
  savePlayerState();
  // Update display and notify clients
  updateDisplay();
  sendStatusToClients();
  // Send success response
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Stream stopped successfully\"}");
}

/**
 * @brief Handle volume request
 * Sets the volume level
 * This function handles HTTP requests to set the volume level. It supports both
 * JSON payload and form data, validates the input, and updates the volume.
 */
void handleVolume() {
  if (server.hasArg("plain")) {
    // Handle JSON payload
    String json = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json);
    // Check for JSON parsing errors
    if (error) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }
    // Check for required parameters
    if (!doc.containsKey("volume")) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameter: volume\"}");
      return;
    }
    // Extract volume value
    int newVolume = doc["volume"];
    // Validate volume range
    if (newVolume < 0 || newVolume > 22) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Volume must be between 0 and 22\"}");
      return;
    }
    // Update volume
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume);  // ESP32-audioI2S uses 0-22 scale
    }
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Send success response
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Volume set successfully\"}");
  } else if (server.hasArg("volume")) {
    // Handle form data
    String vol = server.arg("volume");
    int newVolume = vol.toInt();
    // Validate volume range
    if (newVolume < 0 || newVolume > 22) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Volume must be between 0 and 22\"}");
      return;
    }
    // Update volume
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume);  // ESP32-audioI2S uses 0-22 scale
    }
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Send success response
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Volume set successfully\"}");
  } else {
    // Missing required parameter
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameter: volume\"}");
    return;
  }
}

/**
 * @brief Handle tone request
 * Sets the bass/treble levels
 * This function handles HTTP requests to set the bass/treble levels. It supports
 * JSON payload, validates the input, and updates the tone settings.
 */
void handleTone() {
  if (server.hasArg("plain")) {
    // Handle JSON payload
    String json = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json);
    // Check for JSON parsing errors
    if (error) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }
    bool updated = false;
    // Handle bass setting
    if (doc.containsKey("bass")) {
      int newBass = doc["bass"];
      if (newBass < -6 || newBass > 6) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Bass must be between -6 and 6\"}");
        return;
      }
      bass = newBass;
      updated = true;
    }
    // Handle midrange setting
    if (doc.containsKey("midrange")) {
      int newMidrange = doc["midrange"];
      if (newMidrange < -6 || newMidrange > 6) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Midrange must be between -6 and 6\"}");
        return;
      }
      midrange = newMidrange;
      updated = true;
    }
    // Handle treble setting
    if (doc.containsKey("treble")) {
      int newTreble = doc["treble"];
      if (newTreble < -6 || newTreble > 6) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Treble must be between -6 and 6\"}");
        return;
      }
      treble = newTreble;
      updated = true;
    }
    // Check for missing parameters
    if (!updated) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameter: bass, midrange, or treble\"}");
      return;
    }
    // Apply tone settings to audio using the Audio library's setTone function
    if (audio) {
      // For setTone: gainLowPass (bass), gainBandPass (midrange), gainHighPass (treble)
      audio->setTone(bass, midrange, treble);
    }
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Tone settings updated successfully\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON data\"}");
    return;
  }
}

/**
 * @brief Handle status request
 * Returns the current player status as JSON
 * This function provides the current player status including playback state,
 * current stream information, and volume level in JSON format.
 */
void handleStatus() {
  String status = "{";
  status += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
  status += "\"streamURL\":\"" + String(streamURL) + "\",";
  status += "\"streamName\":\"" + String(streamName) + "\",";
  status += "\"volume\":" + String(volume);
  status += "}";
  // Return status as JSON
  server.send(200, "application/json", status);
}

/**
 * @brief Handle GET request for configuration
 * Returns the current configuration as JSON
 * This function serves the current configuration in JSON format.
 */
void handleGetConfig() {
  // Get current configuration
  String json = "{";
  json += "\"i2s_dout\":" + String(config.i2s_dout) + ",";
  json += "\"i2s_bclk\":" + String(config.i2s_bclk) + ",";
  json += "\"i2s_lrc\":" + String(config.i2s_lrc) + ",";
  json += "\"led_pin\":" + String(config.led_pin) + ",";
  json += "\"rotary_clk\":" + String(config.rotary_clk) + ",";
  json += "\"rotary_dt\":" + String(config.rotary_dt) + ",";
  json += "\"rotary_sw\":" + String(config.rotary_sw) + ",";
  json += "\"board_button\":" + String(config.board_button) + ",";
  json += "\"display_sda\":" + String(config.display_sda) + ",";
  json += "\"display_scl\":" + String(config.display_scl) + ",";
  json += "\"display_width\":" + String(config.display_width) + ",";
  json += "\"display_height\":" + String(config.display_height) + ",";
  json += "\"display_address\":" + String(config.display_address);
  json += "}";
  // Return configuration as JSON
  server.send(200, "application/json", json);
}

/**
 * @brief Handle POST request for configuration
 * Updates the configuration with new JSON data and saves to SPIFFS
 * This function receives a new configuration via HTTP POST, validates it, and saves it to SPIFFS.
 */
void handlePostConfig() {
  // Check for JSON data
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON data\"}");
    return;
  }
  // Parse the JSON data
  String jsonData = server.arg("plain");
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonData);
  // Check for JSON parsing errors
  if (error) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }
  // Update configuration values
  if (doc.containsKey("i2s_dout")) config.i2s_dout = doc["i2s_dout"];
  if (doc.containsKey("i2s_bclk")) config.i2s_bclk = doc["i2s_bclk"];
  if (doc.containsKey("i2s_lrc")) config.i2s_lrc = doc["i2s_lrc"];
  if (doc.containsKey("led_pin")) config.led_pin = doc["led_pin"];
  if (doc.containsKey("rotary_clk")) config.rotary_clk = doc["rotary_clk"];
  if (doc.containsKey("rotary_dt")) config.rotary_dt = doc["rotary_dt"];
  if (doc.containsKey("rotary_sw")) config.rotary_sw = doc["rotary_sw"];
  if (doc.containsKey("board_button")) config.board_button = doc["board_button"];
  if (doc.containsKey("display_sda")) config.display_sda = doc["display_sda"];
  if (doc.containsKey("display_scl")) config.display_scl = doc["display_scl"];
  if (doc.containsKey("display_width")) config.display_width = doc["display_width"];
  if (doc.containsKey("display_height")) config.display_height = doc["display_height"];
  if (doc.containsKey("display_address")) config.display_address = doc["display_address"];
  // Save to SPIFFS
  saveConfig();
  // Return status as JSON
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated successfully\"}");
}

/**
 * @brief Handle export configuration request
 * Exports all JSON configuration files from SPIFFS as a single JSON object
 * This function reads all JSON files from SPIFFS and combines them into a single
 * JSON object where keys are filenames and values are file contents.
 */
void handleExportConfig() {
  // List of configuration files to export
  const char* configFiles[] = {"/config.json", "/wifi.json", "/playlist.json", "/player.json"};
  // Initialize output string
  String output = "{";
  // Process each configuration file
  for (int i = 0; i < 4; i++) {
    const char* filename = configFiles[i];
    // Check if file exists
    if (SPIFFS.exists(filename)) {
      // Open the file
      File file = SPIFFS.open(filename, "r");
      if (file) {
        // Get file size
        size_t size = file.size();
        if (size > 0 && size < 4096) { // Reasonable size limit
          // Allocate buffer for file content
          std::unique_ptr<char[]> buf(new char[size + 1]);
          if (buf) {
            // Read file content
            if (file.readBytes(buf.get(), size) == size) {
              buf[size] = '\0';              
              // Add to main document with filename as key (without leading slash)
              output += "\n\"" + String(filename + 1) + "\":" + String(buf.get());
              if (i < 3) output += ",";
            }
          }
        }
        file.close();
      }
    }
  }
  // Close the JSON object
  output += "}";
  // Send the combined JSON as response
  server.send(200, "application/json", output);
}

/**
 * @brief Handle import configuration request
 * Imports a combined JSON configuration file and saves individual files to SPIFFS
 * This function receives a JSON file containing all configurations and decomposes
 * it into individual config.json, wifi.json, playlist.json, and player.json files.
 */
void handleImportConfig() {
  // Check if request method is POST
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
    return;
  }
  // Check if we have data in the request body
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data received\"}");
    return;
  }
  // Get the JSON data from the request body
  String jsonData = server.arg("plain");
  // Check if data is empty
  if (jsonData.length() == 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No file uploaded\"}");
    return;
  }
  // Parse the JSON data
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.printf("Failed to parse uploaded JSON: %s\n", error.c_str());
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON format\"}");
    return;
  }
  // Process each configuration section
  bool success = true;
  const char* configFiles[] = {"config.json", "wifi.json", "playlist.json", "player.json"};
  for (int i = 0; i < 4; i++) {
    const char* filename = configFiles[i];
    // Check if this section exists in the uploaded data
    if (doc.containsKey(filename)) {
      // Save the JSON content directly to SPIFFS without parsing
      File outFile = SPIFFS.open("/" + String(filename), "w");
      if (outFile) {
        // Get the raw JSON string for this section
        String sectionContent;
        serializeJson(doc[filename], sectionContent);
        // Write the content directly to the file
        if (outFile.print(sectionContent) == 0) {
          Serial.printf("Failed to write %s to file\n", filename);
          success = false;
        }
        outFile.close();
      } else {
        Serial.printf("Failed to open %s for writing\n", filename);
        success = false;
      }
    }
  }
  if (success) {
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration imported successfully\"}");
  } else {
    server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Error importing configuration\"}");
  }
}

/**
 * @brief Generate JSON status string
 * Creates a JSON string with current player status information
 * @return JSON formatted status string
 */
String generateStatusJSON() {
  String status = "{";
  status += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
  status += "\"streamURL\":\"" + String(streamURL) + "\",";
  status += "\"streamName\":\"" + String(streamName) + "\",";
  status += "\"streamTitle\":\"" + String(streamTitle) + "\",";
  status += "\"streamIcyURL\":\"" + String(streamIcyURL) + "\",";
  status += "\"streamIconURL\":\"" + String(streamIconURL) + "\",";
  status += "\"bitrate\":" + String(bitrate) + ",";
  status += "\"volume\":" + String(volume) + ",";
  status += "\"bass\":" + String(bass) + ",";
  status += "\"midrange\":" + String(midrange) + ",";
  status += "\"treble\":" + String(treble);
  status += "}";
  return status;
}

/**
 * @brief Send status to all connected WebSocket clients
 * This function broadcasts the current player status to all connected WebSocket clients.
 * The status includes playback state, stream information, bitrate, and volume.
 */
void sendStatusToClients() {
  // Only broadcast if WebSocket server has clients
  if (webSocket.connectedClients() > 0) {
    // Get current status
    String status = generateStatusJSON();
    // Broadcast status to all connected clients
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
  // Handle WebSocket events
  switch(type) {
    case WStype_CONNECTED:
      // Client connected
      Serial.printf("WebSocket client #%u connected from %d.%d.%d.%d\n", num,
                    webSocket.remoteIP(num)[0], webSocket.remoteIP(num)[1],
                    webSocket.remoteIP(num)[2], webSocket.remoteIP(num)[3]);
      // Send current status to newly connected client
      {
        // Get current status
        String status = generateStatusJSON();
        // Send status to newly connected client
        webSocket.sendTXT(num, status);
      }
      break;
    case WStype_DISCONNECTED:
      // Client disconnected
      Serial.printf("WebSocket client #%u disconnected\n", num);
      // Add a small delay to ensure proper cleanup
      delay(10);
      break;
    case WStype_TEXT:
      // Text message received
      Serial.printf("WebSocket client #%u text: %s\n", num, payload);
      break;
    default:
      break;
  }
}

/**
 * @brief Update the OLED display with current status
 * Shows playback status, current stream, volume level, and playlist selection
 * This function updates the OLED display with the current player status. When playing,
 * it shows the station name, stream title, bitrate, and volume. When stopped, it shows
 * the selected playlist item and volume. It also implements scrolling text for long strings.
 */
void updateDisplay() {
  // If display is off, don't update
  if (!displayOn) {
    return;
  }
  // Get text bounds for display
  int16_t x1, y1;
  uint16_t w, h;
  // Clear the display buffer
  display.clearDisplay();
  if (isPlaying) {
    // Display when playing
    display.setCursor(0, 12);
    // Fixed '>' character
    display.print(">");
    // Display stream title (first line) with scrolling
    String title = String(streamTitle);
    if (title.length() == 0) {
      title = String(streamName);
    }
    // Scroll title if too long for display (excluding the '>' character)
    // 16 chars fit on a 128px display with '>' and some margin
    // Calculate how many characters we can display (14 chars = 84 pixels)
    int maxDisplayChars = 14;
    if (title.length() > maxDisplayChars) {
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
        titleScrollOffset++;
        // Reset scroll when we've shown the entire text plus " ~~~ "
        // Calculate based on pixels: each character is 8 pixels wide in Spleen8x16 font
        int totalPixels = (title.length() + 4) * 8;  // +4 for " ~~~ "
        int displayWidth = maxDisplayChars * 8;  // 14 characters * 8 pixels
        if (titleScrollOffset > (totalPixels + displayWidth)) {
          titleScrollOffset = 0;
        }
        lastTitleScrollTime = millis();
      }
      // Display scrolled text with pixel positioning
      String displayText = title + " ~~~ " + title;
      // Create a temporary string that's long enough to fill the display
      String tempText = displayText + displayText;  // Double it to ensure enough content
      
      // Calculate starting position based on scroll offset
      int startPixel = titleScrollOffset % (displayText.length() * 8);  // 8 pixels per char
      int startChar = startPixel / 8;
      int pixelOffset = startPixel % 8;
      
      // Instead of substring, we'll use pixel positioning
      display.setCursor(16 - pixelOffset, 12);

      // Display text with pixel offset
      String visibleText = tempText.substring(startChar, startChar + maxDisplayChars);
      display.print(visibleText);
    } else {
      // Display title without scrolling
      display.setCursor(16, 12);
      display.print(title);
    }
    // Display stream name (second line)
    display.setCursor(0, 30);
    String stationName = String(streamName);
    // 16 chars fit on a 128px display
    if (stationName.length() > 16) {
      display.print(stationName.substring(0, 16));
    } else {
      display.print(stationName);
    }
    // Display volume and bitrate on third line
    if (config.display_height >= 32) {
      // Display volume and bitrate on third line
      char volStr[20];
      sprintf(volStr, "Vol %2d", volume);
      display.setCursor(0, 45);
      display.print(volStr);
      // Display bitrate on the same line
      if (bitrate > 0) {
        char bitrateStr[20];
        sprintf(bitrateStr, "%3d kbps", bitrate);
        display.getTextBounds(bitrateStr, 0, 45, &x1, &y1, &w, &h);
        display.setCursor(display.width() - w - 1, 45);
        display.print(bitrateStr);
      }
      // Display IP address on the last line, centered
      String ipString;
      if (WiFi.status() == WL_CONNECTED) {
        ipString = WiFi.localIP().toString();
      } else {
        ipString = "No IP";
      }
      // Center the IP address
      display.getTextBounds(ipString, 0, 62, &x1, &y1, &w, &h);
      int x = (display.width() - w) / 2;
      if (x < 0) x = 0;
      // Center the IP address
      display.setCursor(x, 62);
      display.print(ipString);
    }
  } else {
    // Display when stopped
    display.setCursor(32, 12);
    display.print("NetTuner");
    // Display current stream name (second line)
    display.setCursor(0, 30);
    if (strlen(streamName) > 0) {
      String currentStream = String(streamName);
      // 16 chars fit on a 128px display
      if (currentStream.length() > 16) {
        display.print(currentStream.substring(0, 16));
      } else {
        display.print(currentStream);
      }
    } else if (playlistCount > 0 && currentSelection < playlistCount) {
      String playlistName = String(playlist[currentSelection].name);
      // 16 chars fit on a 128px display
      if (playlistName.length() > 16) {
        display.print(playlistName.substring(0, 16));
      } else {
        display.print(playlistName);
      }
    } else {
      // No stream is currently found in playlist
      display.setCursor(34, 30);
      display.print("No streams");
    }
    // Display volume on third line
    if (config.display_height >= 32) {
      // Display volume and bitrate on third line
      char volStr[20];
      sprintf(volStr, "Vol %2d", volume);
      display.setCursor(0, 45);
      display.print(volStr);
      // Display IP address on the last line, centered
      String ipString;
      if (WiFi.status() == WL_CONNECTED) {
        ipString = WiFi.localIP().toString();
      } else {
        ipString = "No IP";
      }
      // Center the IP address
      display.getTextBounds(ipString, 0, 62, &x1, &y1, &w, &h);
      int x = (display.width() - w) / 2;
      if (x < 0) x = 0;
      // Center the IP address
      display.setCursor(x, 62);
      display.print(ipString);
    }
  }
  // Send buffer to display
  display.display();
}
