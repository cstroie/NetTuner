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
char streamURL[256] = "";          ///< URL of currently playing stream
char streamName[128] = "";         ///< Name of currently playing stream
char streamTitle[128] = "";        ///< Current stream title
int bitrate = 0;                   ///< Current stream bitrate
volatile bool isPlaying = false;   ///< Playback status flag (volatile for core synchronization)
int volume = 11;                   ///< Volume level (0-22)
int bass = 0;                      ///< Bass level (-6 to 6 dB)
int midrange = 0;                  ///< Midrange level (-6 to 6 dB)
int treble = 0;                    ///< Treble level (-6 to 6 dB)
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
    if (strcmp(streamName, info) != 0) {
      strncpy(streamName, info, sizeof(streamName) - 1);
      streamName[sizeof(streamName) - 1] = '\0';
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
// Default pin definitions
#define DEFAULT_I2S_DOUT         26  ///< I2S Data Out pin
#define DEFAULT_I2S_BCLK         27  ///< I2S Bit Clock pin
#define DEFAULT_I2S_LRC          25  ///< I2S Left/Right Clock pin
#define DEFAULT_LED_PIN           2  ///< ESP32 internal LED pin
#define DEFAULT_ROTARY_CLK       18  ///< Rotary encoder clock pin
#define DEFAULT_ROTARY_DT        19  ///< Rotary encoder data pin
#define DEFAULT_ROTARY_SW        23  ///< Rotary encoder switch pin
#define DEFAULT_BOARD_BUTTON      0  ///< ESP32 board button pin (with internal pull-up resistor)
#define DEFAULT_DISPLAY_SDA      21  ///< OLED display SDA pin
#define DEFAULT_DISPLAY_SCL      22  ///< OLED display SCL pin
#define DEFAULT_DISPLAY_WIDTH   128  ///< OLED display width
#define DEFAULT_DISPLAY_HEIGHT   64  ///< OLED display height
#define DEFAULT_DISPLAY_ADDR   0x3C  ///< OLED display I2C address

/**
 * @brief Configuration structure
 * Stores hardware configuration settings
 */
struct Config {
  int i2s_dout;
  int i2s_bclk;
  int i2s_lrc;
  int led_pin;
  int rotary_clk;
  int rotary_dt;
  int rotary_sw;
  int board_button;
  int display_sda;
  int display_scl;
  int display_width;
  int display_height;
  int display_address;
};

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

/**
 * @brief OLED display instance
 * SSD1306 OLED display connected via I2C
 */
Adafruit_SSD1306 display(config.display_width, config.display_height, &Wire, -1);


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
    
    int CLK = digitalRead(config.rotary_clk);  // Read clock signal
    int DT = digitalRead(config.rotary_dt);    // Read data signal
    
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

// Global rotary encoder instance
RotaryEncoder rotaryEncoder;

// Function declarations
void loadConfig();
void saveConfig();

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
void loadConfig();
void saveConfig();

// MPD functions
void handleMPDClient();
String mpdResponseOK();
String mpdResponseError(const String& message);
void handleMPDCommand(const String& command);

// Web server handlers
void handleRoot();
void handlePlaylistPage();
void handleConfigPage();
void handleAboutPage();
void handleSimpleWebPage();
void handleGetStreams();
void handlePostStreams();
void handlePlay();
void handleStop();
void handleVolume();
void handleTone();
void handleStatus();
void handleGetConfig();
void handlePostConfig();
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
 * @brief Arduino main loop function
 * Handles web server requests, WebSocket events, rotary encoder input, and MPD commands
 * This is the main application loop that runs continuously after setup()
 */
void handleBoardButton() {
  static bool lastButtonState = HIGH;  // Keep track of button state
  static unsigned long lastDebounceTime = 0;  // Last time the button was pressed
  const unsigned long debounceDelay = 50;  // Debounce time in milliseconds

  // Handle board button press for play/stop toggle
  int buttonReading = digitalRead(config.board_button);
  
  // Check if button state changed (debounce)
  if (buttonReading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  // If button state has been stable for debounce delay
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // If button is pressed (LOW due to pull-up)
    if (buttonReading == LOW && lastButtonState == HIGH) {
      // Toggle play/stop
      if (isPlaying) {
        stopStream();
      } else {
        // If we have a current stream, resume it
        if (strlen(streamURL) > 0) {
          startStream();
        } 
        // Otherwise, if we have playlist items, play the selected one
        else if (playlistCount > 0 && currentSelection < playlistCount) {
          startStream(playlist[currentSelection].url, playlist[currentSelection].name);
        }
      }
      updateDisplay();
      sendStatusToClients();
    }
  }
  
  lastButtonState = buttonReading;
}

void loop() {
  static unsigned long streamStoppedTime = 0;

  handleRotary();          // Process rotary encoder input
  server.handleClient();   // Process incoming web requests
  webSocket.loop();        // Process WebSocket events
  handleMPDClient();       // Process MPD commands
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
 * This function reads WiFi credentials from wifi.json in SPIFFS and populates
 * the ssid and password arrays. It supports the new JSON array format.
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
  if (size > 2048) {  // Increased size for array format
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
  
  DynamicJsonDocument doc(2048);  // Increased size for array format
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("Failed to parse WiFi config JSON: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Handle the new JSON array format [{"ssid": "name", "password": "pass"}, ...]
  if (doc.is<JsonArray>()) {
    JsonArray networks = doc.as<JsonArray>();
    wifiNetworkCount = 0;
    
    for (JsonObject network : networks) {
      if (wifiNetworkCount >= MAX_WIFI_NETWORKS) break;
      
      if (network.containsKey("ssid")) {
        const char* ssidValue = network["ssid"];
        if (ssidValue) {
          strncpy(ssid[wifiNetworkCount], ssidValue, sizeof(ssid[wifiNetworkCount]) - 1);
          ssid[wifiNetworkCount][sizeof(ssid[wifiNetworkCount]) - 1] = '\0';
        } else {
          ssid[wifiNetworkCount][0] = '\0';
        }
        
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
        
        wifiNetworkCount++;
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
 * @brief Load configuration from SPIFFS
 * This function reads configuration from config.json in SPIFFS
 */
void loadConfig() {
  if (!SPIFFS.exists("/config.json")) {
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
  
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Failed to open config file");
    return;
  }
  
  size_t size = file.size();
  if (size > 1024) {
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
  if (!buf) {
    Serial.println("Error: Failed to allocate memory for config file");
    file.close();
    return;
  }
  
  if (file.readBytes(buf.get(), size) != size) {
    Serial.println("Failed to read config file");
    file.close();
    return;
  }
  buf[size] = '\0';
  file.close();
  // DEBUG
  Serial.print("Config JSON: ");
  Serial.println(String(buf.get()));
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("Failed to parse config JSON: ");
    Serial.println(error.c_str());
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
  
  Serial.println("Loaded configuration from SPIFFS");
}

/**
 * @brief Save configuration to SPIFFS
 * This function saves the current configuration to config.json in SPIFFS
 */
void saveConfig() {
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
  
  File file = SPIFFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  
  size_t bytesWritten = serializeJson(doc, file);
  if (bytesWritten == 0) {
    Serial.println("Failed to write config to file");
    file.close();
    return;
  }
  file.close();
  
  Serial.println("Saved configuration to SPIFFS");
}

/**
 * @brief Save WiFi credentials to SPIFFS
 * This function saves the current WiFi credentials to wifi.json in SPIFFS.
 * It stores networks in the new JSON array format.
 */
void saveWiFiCredentials() {
  DynamicJsonDocument doc(2048); // Increased size for array format
  JsonArray networks = doc.to<JsonArray>();
  
  // Save networks in the new JSON array format [{"ssid": "name", "password": "pass"}, ...]
  for (int i = 0; i < wifiNetworkCount; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = ssid[i];
    if (strlen(password[i]) > 0) {
      network["password"] = password[i];
    }
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
  // Stop any currently playing stream
  if (audio) {
    // Stop first
    audio->stopSong();
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
  bitrate = 0;                   // Clear bitrate
  
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
 * @brief Rotary encoder interrupt service routine
 * Called when the rotary encoder position changes
 * Kept minimal to reduce interrupt execution time
 * This function is called by the interrupt handler and delegates to the
 * RotaryEncoder class for processing.
 */
void rotaryISR() {
  rotaryEncoder.handleRotation();
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
 * This function manages the OLED display timeout to conserve power. When not playing,
 * the display turns off after 30 seconds of inactivity. The display turns back on
 * when there's activity or when playback starts.
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
 * This function handles HTTP requests to start playing a stream. It supports both
 * JSON payload and form data, validates the input, and starts the stream.
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
 * This function handles HTTP requests to stop the currently playing stream.
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
 * This function handles HTTP requests to set the volume level. It supports both
 * JSON payload and form data, validates the input, and updates the volume.
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
    
    if (newVolume < 0 || newVolume > 22) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Volume must be between 0 and 22\"}");
      return;
    }
    
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume);  // ESP32-audioI2S uses 0-22 scale
    }
    updateDisplay();
    sendStatusToClients();  // Notify clients of status change
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Volume set successfully\"}");
  } else if (server.hasArg("volume")) {
    // Handle form data
    String vol = server.arg("volume");
    int newVolume = vol.toInt();
    
    if (newVolume < 0 || newVolume > 22) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Volume must be between 0 and 22\"}");
      return;
    }
    
    volume = newVolume;
    if (audio) {
      audio->setVolume(volume);  // ESP32-audioI2S uses 0-22 scale
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
    
    if (!updated) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameter: bass, midrange, or treble\"}");
      return;
    }
    
    // Apply tone settings to audio using the Audio library's setTone function
    if (audio) {
      // For setTone: gainLowPass (bass), gainBandPass (midrange), gainHighPass (treble)
      audio->setTone(bass, midrange, treble);
    }
    
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
  server.send(200, "application/json", status);
}

/**
 * @brief Handle GET request for configuration
 * Returns the current configuration as JSON
 * This function serves the current configuration in JSON format.
 */
void handleGetConfig() {
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
  server.send(200, "application/json", json);
}

/**
 * @brief Handle POST request for configuration
 * Updates the configuration with new JSON data and saves to SPIFFS
 * This function receives a new configuration via HTTP POST, validates it, and saves it to SPIFFS.
 */
void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON data\"}");
    return;
  }
  
  String jsonData = server.arg("plain");
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonData);
  
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
  
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated successfully\"}");
}

/**
 * @brief Send status to all connected WebSocket clients
 * This function broadcasts the current player status to all connected WebSocket clients.
 * The status includes playback state, stream information, bitrate, and volume.
 */
void sendStatusToClients() {
  String status = "{";
  status += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
  status += "\"streamURL\":\"" + String(streamURL) + "\",";
  status += "\"streamName\":\"" + String(streamName) + "\",";
  status += "\"streamTitle\":\"" + String(streamTitle) + "\",";
  status += "\"bitrate\":" + String(bitrate / 1000) + ",";
  status += "\"volume\":" + String(volume) + ",";
  status += "\"bass\":" + String(bass) + ",";
  status += "\"midrange\":" + String(midrange) + ",";
  status += "\"treble\":" + String(treble);
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
        status += "\"streamURL\":\"" + String(streamURL) + "\",";
        status += "\"streamName\":\"" + String(streamName) + "\",";
        status += "\"streamTitle\":\"" + String(streamTitle) + "\",";
        status += "\"bitrate\":" + String(bitrate / 1000) + ",";
        status += "\"volume\":" + String(volume) + ",";
        status += "\"bass\":" + String(bass) + ",";
        status += "\"midrange\":" + String(midrange) + ",";
        status += "\"treble\":" + String(treble);
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
 * This function updates the OLED display with the current player status. When playing,
 * it shows the station name, stream title, bitrate, and volume. When stopped, it shows
 * the selected playlist item and volume. It also implements scrolling text for long strings.
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
    String stationName = String(streamName);
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
 * This function handles connections from MPD clients and processes MPD protocol commands.
 * It manages one client connection at a time and processes commands as they arrive.
 */
// MPD command list state variables
bool inCommandList = false;
bool commandListOK = false;
String commandList[50];  // Buffer for command list (max 50 commands)
int commandListCount = 0;

// MPD idle state variables
bool inIdleMode = false;
unsigned long lastTitleHash = 0;
unsigned long lastStatusHash = 0;

void handleMPDClient() {
  if (mpdServer.hasClient()) {
    if (!mpdClient || !mpdClient.connected()) {
      if (mpdClient) {
        mpdClient.stop();  // Ensure previous client is properly closed
      }
      mpdClient = mpdServer.available();
      // Send MPD welcome message
      if (mpdClient && mpdClient.connected()) {
        mpdClient.print("OK MPD 0.23.0\n");
      }
      // Reset command list state when new client connects
      inCommandList = false;
      commandListOK = false;
      commandListCount = 0;
      inIdleMode = false;
    } else {
      // Reject connection if we already have a client
      WiFiClient rejectedClient = mpdServer.available();
      rejectedClient.stop();  // Properly close rejected connection
    }
  }
  
  // Check if client disconnected unexpectedly
  if (mpdClient && !mpdClient.connected()) {
    mpdClient.stop();
    // Reset command list state when client disconnects
    inCommandList = false;
    commandListOK = false;
    commandListCount = 0;
    inIdleMode = false;
  }
  
  if (mpdClient && mpdClient.connected()) {
    // Check for idle mode events
    if (inIdleMode) {
      // Check for title changes
      unsigned long currentTitleHash = 0;
      for (int i = 0; streamTitle[i]; i++) {
        currentTitleHash = currentTitleHash * 31 + streamTitle[i];
      }
      
      // Check for status changes
      unsigned long currentStatusHash = isPlaying ? 1 : 0;
      currentStatusHash = currentStatusHash * 31 + volume;
      
      bool sendIdleResponse = false;
      String idleChanges = "";
      
      // Check for title change
      if (currentTitleHash != lastTitleHash) {
        idleChanges += "changed: playlist\n";
        lastTitleHash = currentTitleHash;
        sendIdleResponse = true;
      }
      
      // Check for status change
      if (currentStatusHash != lastStatusHash) {
        idleChanges += "changed: player\n";
        idleChanges += "changed: mixer\n";
        lastStatusHash = currentStatusHash;
        sendIdleResponse = true;
      }
      
      // Send idle response if there are changes
      if (sendIdleResponse) {
        mpdClient.print(idleChanges);
        mpdClient.print(mpdResponseOK());
        inIdleMode = false;
      }
      
      // Check if there's data available (for noidle command)
      if (mpdClient.available()) {
        String command = mpdClient.readStringUntil('\n');
        command.trim();
        Serial.println("MPD Command: " + command);
        
        // Handle noidle command to exit idle mode
        if (command == "noidle") {
          inIdleMode = false;
          mpdClient.print(mpdResponseOK());
          return;
        }
      } else {
        // Stay in idle mode and return early
        return;
      }
    }
    
    if (mpdClient.available()) {
      String command = mpdClient.readStringUntil('\n');
      command.trim();
      Serial.println("MPD Command: " + command);
      
      // Handle command list mode
      if (inCommandList) {
        if (command == "command_list_end") {
          // Execute all buffered commands
          for (int i = 0; i < commandListCount; i++) {
            handleMPDCommand(commandList[i]);
          }
          // Reset command list state
          inCommandList = false;
          commandListOK = false;
          commandListCount = 0;
          mpdClient.print(mpdResponseOK());
        } else {
          // Buffer the command
          if (commandListCount < 50) {
            commandList[commandListCount] = command;
            commandListCount++;
          } else {
            // Command list too long, send error
            inCommandList = false;
            commandListOK = false;
            commandListCount = 0;
            mpdClient.print(mpdResponseError("Command list too long"));
          }
        }
      } else {
        // Normal command processing
        handleMPDCommand(command);
      }
    }
  }
}

/**
 * @brief Generate MPD response
 * @param isError Whether this is an error response
 * @param message Error message (only used for error responses)
 * @return Response string
 */
String mpdResponse(bool isError = false, const String& message = "") {
  if (isError) {
    return "ACK [5@0] {} " + message + "\n";
  }
  
  // Check if in command list mode
  if (inCommandList) {
    // Send OK for each command if in command_list_ok_begin mode
    if (commandListOK) {
      return "list_OK\n";
    }
    else 
      return "";
  }
  // Standard OK response
  return "OK\n";
}

/**
 * @brief Generate MPD OK response
 * @return OK response string
 */
String mpdResponseOK() {
  return mpdResponse(false, "");
}

/**
 * @brief Generate MPD error response
 * @param message Error message
 * @return Error response string
 */
String mpdResponseError(const String& message) {
  return mpdResponse(true, message);
}

/**
 * @brief Send playlist information with configurable detail level
 * Sends playlist information with different levels of metadata
 * @param detailLevel 0=minimal (file+title), 1=simple (file+title+lastmod), 2=full (file+title+id+pos+lastmod)
 */
void sendPlaylistInfo(int detailLevel = 2) {
  for (int i = 0; i < playlistCount; i++) {
    // For detailLevel 0, only file and title are sent (minimal)
    mpdClient.print("file: " + String(playlist[i].url) + "\n");
    mpdClient.print("Title: " + String(playlist[i].name) + "\n");
    
    if (detailLevel >= 2) {
      // Full detail level
      mpdClient.print("Id: " + String(i) + "\n");
      mpdClient.print("Pos: " + String(i) + "\n");
    }

    if (detailLevel >= 1) {
      // Simple detail level
      mpdClient.print("Last-Modified: 2025-01-01T00:00:00Z\n");
    }
  }
}

/**
 * @brief Handle MPD search/find commands
 * Processes search and find commands with partial or exact matching
 * @param command The full command string
 * @param exactMatch Whether to perform exact matching (find) or partial matching (search)
 */
void handleMPDSearchCommand(const String& command, bool exactMatch) {
  // Determine command prefix length (search=6, find=4)
  int prefixLength = command.startsWith("search") ? 6 : 4;
  
  if (command.length() > prefixLength + 1) {
    String searchTerm = command.substring(prefixLength + 1);
    searchTerm.trim();
    
    // Extract search string (everything after the first space)
    int firstSpace = searchTerm.indexOf(' ');
    if (firstSpace != -1) {
      searchTerm = searchTerm.substring(firstSpace + 1);
      searchTerm.trim();
    }
    
    // Remove quotes if present
    if (searchTerm.startsWith("\"") && searchTerm.endsWith("\"") && searchTerm.length() >= 2) {
      searchTerm = searchTerm.substring(1, searchTerm.length() - 1);
    }
    
    // Search in playlist names
    for (int i = 0; i < playlistCount; i++) {
      String playlistName = String(playlist[i].name);
      // Convert both to lowercase for case-insensitive comparison
      String lowerName = playlistName;
      lowerName.toLowerCase();
      String lowerSearch = searchTerm;
      lowerSearch.toLowerCase();
      
      bool match = false;
      if (exactMatch) {
        // Exact match for find command
        match = (lowerName == lowerSearch);
      } else {
        // Partial match for search command
        match = (lowerName.indexOf(lowerSearch) != -1);
      }
      
      if (match) {
        mpdClient.print("file: " + String(playlist[i].url) + "\n");
        mpdClient.print("Title: " + String(playlist[i].name) + "\n");
        mpdClient.print("Last-Modified: 2025-01-01T00:00:00Z\n");
      }
    }
  }
}

/**
 * @brief Handle MPD commands
 * Processes MPD protocol commands
 * @param command The command string to process
 * This function processes MPD protocol commands and controls the player accordingly.
 * It supports a subset of MPD commands including playback control, volume control,
 * playlist management, and status queries.
 */
void handleMPDCommand(const String& command) {
if (command.startsWith("stop")) {
    // Stop command
    stopStream();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("pause")) {
    // Pause command (treat as stop for simplicity)
    stopStream();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("status")) {
    // Status command
    int volPercent = map(volume, 0, 22, 0, 100);
    mpdClient.print("volume: " + String(volPercent) + "\n");
    mpdClient.print("repeat: 0\n");
    mpdClient.print("random: 0\n");
    mpdClient.print("single: 0\n");
    mpdClient.print("consume: 0\n");
    mpdClient.print("playlist: 1\n");
    mpdClient.print("playlistlength: " + String(playlistCount) + "\n");
    mpdClient.print("mixrampdb: 0.000000\n");
    mpdClient.print("state: " + String(isPlaying ? "play" : "stop") + "\n");
    if (isPlaying && strlen(streamName) > 0) {
      mpdClient.print("song: " + String(currentSelection) + "\n");
      mpdClient.print("songid: " + String(currentSelection) + "\n");
      mpdClient.print("time: 0:0\n");
      mpdClient.print("elapsed: 0.000\n");
      mpdClient.print("bitrate: " + String(bitrate / 1000) + "\n");
      mpdClient.print("audio: 44100:16:2\n");
      mpdClient.print("nextsong: " + String((currentSelection + 1) % playlistCount) + "\n");
      mpdClient.print("nextsongid: " + String((currentSelection + 1) % playlistCount) + "\n");
    }
    mpdClient.print("updating_db: 0\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("currentsong")) {
    // Current song command
    if (isPlaying && strlen(streamName) > 0) {
      mpdClient.print("file: " + String(streamURL) + "\n");
      if (strlen(streamTitle) > 0) {
        mpdClient.print("Title: " + String(streamTitle) + "\n");
      } else {
        mpdClient.print("Title: " + String(streamName) + "\n");
      }
      mpdClient.print("Id: " + String(currentSelection) + "\n");
      mpdClient.print("Pos: " + String(currentSelection) + "\n");
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistinfo")) {
    // Playlist info command
    sendPlaylistInfo(2); // Full detail
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
      sendPlaylistInfo(2); // Full detail
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("lsinfo")) {
    // List info command
    sendPlaylistInfo(1); // Simple detail
    mpdClient.print(mpdResponseOK());
  } else  if (command.startsWith("play")) {
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
  } else if (command.startsWith("setvol")) {
    // Set volume command
    if (command.length() > 7) {
      String volumeStr = command.substring(7);
      volumeStr.trim();
      
      // Remove quotes if present
      if (volumeStr.startsWith("\"") && volumeStr.endsWith("\"") && volumeStr.length() >= 2) {
        volumeStr = volumeStr.substring(1, volumeStr.length() - 1);
      }
      
      int newVolume = volumeStr.toInt();
      if (newVolume >= 0 && newVolume <= 100) {
        volume = map(newVolume, 0, 100, 0, 22);  // Map 0-100 to 0-22 scale
        if (audio) {
          audio->setVolume(volume);  // ESP32-audioI2S uses 0-22 scale
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
    //mpdClient.print("attribute: allowed_formats=\n");
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
    const char* supportedCommands[] = {
      "add",
      "clear",
      "close",
      "currentsong",
      "delete",
      "disableoutput",
      "enableoutput",
      "find",
      "idle",
      "kill",
      "list",
      "listallinfo",
      "listplaylistinfo",
      "load",
      "lsinfo",
      "next",
      "notcommands",
      "outputs",
      "password",
      "pause",
      "ping",
      "play",
      "playid",
      "playlistid",
      "playlistinfo",
      "plchanges",
      "previous",
      "save",
      "search",
      "seek",
      "seekid",
      "setvol",
      "stats",
      "status",
      "stop",
      "tagtypes",
      "update"
    };
    
    const int commandCount = sizeof(supportedCommands) / sizeof(supportedCommands[0]);
    
    for (int i = 0; i < commandCount; i++) {
      mpdClient.print("command: ");
      mpdClient.print(supportedCommands[i]);
      mpdClient.print("\n");
    }
    
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("notcommands")) {
    // Not commands command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("stats")) {
    // Stats command
    mpdClient.print("artists: 0\n");
    mpdClient.print("albums: 0\n");
    mpdClient.print("songs: " + String(playlistCount) + "\n");
    mpdClient.print("uptime: 0\n");
    mpdClient.print("playtime: 0\n");
    mpdClient.print("db_playtime: 0\n");
    mpdClient.print("db_update: 0\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("ping")) {
    // Ping command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("password")) {
    // Password command (not implemented)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("kill")) {
    // Kill command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("update")) {
    // Update command (not implemented for this player)
    mpdClient.print("updating_db: 1\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("listallinfo")) {
    // List all info command
    sendPlaylistInfo(1); // Simple detail
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("listplaylistinfo")) {
    // List playlist info command
    sendPlaylistInfo(0); // Minimal detail
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("list")) {
    // List command
    if (command.length() > 5) {
      String tagType = command.substring(5);
      tagType.trim();
      if (tagType == "artist") {
        // Return empty list for artist (no local database)
      } else if (tagType == "album") {
        // Return empty list for album (no local database)
      } else if (tagType == "title") {
        // Return playlist titles
        for (int i = 0; i < playlistCount; i++) {
          mpdClient.print("Title: " + String(playlist[i].name) + "\n");
        }
      }
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("search")) {
    // Search command (partial match)
    handleMPDSearchCommand(command, false);
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("find")) {
    // Find command (exact match)
    handleMPDSearchCommand(command, true);
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playid")) {
    // Play ID command
    if (command.length() > 7) {
      int id = command.substring(7).toInt();
      if (id >= 0 && id < playlistCount) {
        currentSelection = id;
        startStream(playlist[id].url, playlist[id].name);
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("No such song"));
      }
    } else if (playlistCount > 0 && currentSelection < playlistCount) {
      startStream(playlist[currentSelection].url, playlist[currentSelection].name);
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
    }
  } else if (command.startsWith("seek")) {
    // Seek command (not implemented for streaming)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("seekid")) {
    // Seek ID command (not implemented for streaming)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("tagtypes")) {
    // Tag types command
    if (command.equals("tagtypes \"all\"") || command.equals("tagtypes \"clear\"")) {
      // These commands simply return OK
      mpdClient.print(mpdResponseOK());
    } else {
      // Default tagtypes response
      const char* supportedTagTypes[] = {
        "Artist",
        "Album",
        "Title",
        "Track",
        "Name",
        "Genre",
        "Date",
        "Composer",
        "Performer",
        "Comment",
        "Disc",
        "MUSICBRAINZ_ARTISTID",
        "MUSICBRAINZ_ALBUMID",
        "MUSICBRAINZ_ALBUMARTISTID",
        "MUSICBRAINZ_TRACKID"
      };
      
      const int tagTypeCount = sizeof(supportedTagTypes) / sizeof(supportedTagTypes[0]);
      
      for (int i = 0; i < tagTypeCount; i++) {
        mpdClient.print("tagtype: ");
        mpdClient.print(supportedTagTypes[i]);
        mpdClient.print("\n");
      }
      mpdClient.print(mpdResponseOK());
    }
  } else if (command.startsWith("plchanges")) {
    // Playlist changes command
    // For simplicity, we'll return the entire playlist (as if all entries changed)
    sendPlaylistInfo();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("idle")) {
    // Idle command - enter idle mode and wait for changes
    inIdleMode = true;
    
    // Initialize hashes for tracking changes
    lastTitleHash = 0;
    for (int i = 0; streamTitle[i]; i++) {
      lastTitleHash = lastTitleHash * 31 + streamTitle[i];
    }
    
    lastStatusHash = isPlaying ? 1 : 0;
    lastStatusHash = lastStatusHash * 31 + volume;
    
    // Don't send immediate response - wait for changes
    return;
  } else if (command.startsWith("noidle")) {
    // Noidle command
    inIdleMode = false;
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("close")) {
    // Close command
    mpdClient.print(mpdResponseOK());
    mpdClient.stop();
  } else if (command.startsWith("command_list_begin")) {
    // Start command list mode
    inCommandList = true;
    commandListOK = false;
    commandListCount = 0;
    // Don't send OK yet, wait for command_list_end
  } else if (command.startsWith("command_list_ok_begin")) {
    // Start command list mode with OK responses
    inCommandList = true;
    commandListOK = true;
    commandListCount = 0;
    // Don't send OK yet, wait for command_list_end
  } else if (command.startsWith("command_list_end")) {
    // This should not happen outside of command list mode
    mpdClient.print(mpdResponseError("Not in command list mode"));
  } else if (command.length() == 0) {
    // Empty command
    mpdClient.print(mpdResponseOK());
  } else {
    // Unknown command
    mpdClient.print(mpdResponseError("Unknown command"));
  }
}
