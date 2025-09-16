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

#include "pins.h"
#include "main.h"
#include "mpd.h"
#include "display.h"
#include "rotary.h"
#include "player.h"
#include "playlist.h"
#include "touch.h"

// Spleen fonts https://www.onlinewebfonts.com/icon
#include "Spleen6x12.h" 
#include "Spleen8x16.h" 
#include "Spleen16x32.h"
#include <ESPmDNS.h>
#include <HTTPClient.h>


// Global variables definitions
char ssid[MAX_WIFI_NETWORKS][64] = {""};
char password[MAX_WIFI_NETWORKS][64] = {""};
int wifiNetworkCount = 0;
WebServer server(80);
WebSocketsServer webSocket(81);
WiFiServer mpdServer(6600);
const char* BUILD_TIME = __DATE__ "T" __TIME__"Z";
String previousStatus = "";




Adafruit_SSD1306* displayOLED;
Display* display;
RotaryEncoder rotaryEncoder;
TaskHandle_t audioTaskHandle = NULL;

Player player;
 
// Touch buttons
TouchButton* touchPlay = nullptr;
TouchButton* touchNext = nullptr;
TouchButton* touchPrev = nullptr;

// Flag to indicate board button press
static volatile bool boardButtonPressed = false;

// MPD Interface instance
MPDInterface mpdInterface(mpdServer, player);

// Configuration structure definition
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
  0, // Default display type (OLED_128x64)
  DEFAULT_DISPLAY_ADDR,
  30, // Default display timeout (30 seconds)
  DEFAULT_TOUCH_PLAY,
  DEFAULT_TOUCH_NEXT,
  DEFAULT_TOUCH_PREV,
  DEFAULT_TOUCH_THRESHOLD,
  DEFAULT_TOUCH_DEBOUNCE
};



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
    if (strcmp(player.getStreamTitle(), info) != 0) {
      player.setStreamTitle(info);
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
    if (strcmp(player.getStreamName(), info) != 0) {
      player.setStreamName(info);
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
    if (newBitrate > 0 && newBitrate != player.getBitrate()) {
      player.setBitrate(newBitrate);
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
      // Check if the URL ends with common image extensions
      if (urlPart.endsWith(".png") ||
          urlPart.endsWith(".jpg") ||
          urlPart.endsWith(".jpeg") ||
          urlPart.endsWith(".ico")) {
        // Store the cover image URL
        player.setStreamIconUrl(urlPart.c_str());
        Serial.print("Cover image URL: ");
        Serial.println(player.getStreamIconUrl());
        // Notify clients of the new cover image
        sendStatusToClients();
      }
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
    player.setStreamIcyUrl(info);
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
  // Null-terminate the buffer
  buf[size] = '\0';
  file.close();
  // Parse the JSON document
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.printf("Failed to parse JSON file %s: %s\n", filename, error.c_str());
    return false;
  }
  // Successfully read and parsed the JSON file
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
  // Successfully wrote the JSON file
  return true;
}

/**
 * @brief Send JSON response with status and message
 * Helper function to send standardized JSON responses
 * @param status Status string ("success" or "error")
 * @param message Human-readable message
 * @param code HTTP status code (default 200 for success, 400 for error)
 */
void sendJsonResponse(const String& status, const String& message, int code = -1) {
  // If code not specified, determine based on status
  if (code == -1) {
    code = (status == "success") ? 200 : 400;
  }
  // Create JSON response
  DynamicJsonDocument doc(256);
  doc["status"] = status;
  doc["message"] = message;
  String json;
  serializeJson(doc, json);
  server.send(code, "application/json", json);
}


/**
 * @brief Handle WiFi configuration API request
 * Returns the current WiFi configuration as JSON
 * This function provides the list of configured WiFi networks in JSON format
 */
void handleWiFiConfig() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  DynamicJsonDocument doc(1024);
  // Create JSON array
  JsonArray array = doc.to<JsonArray>();
  // Populate JSON array with configured network SSIDs
  for (int i = 0; i < wifiNetworkCount; i++) {
    array.add(String(ssid[i]));
    // Yield to other tasks during long operations
    yield();
  }
  // Serialize JSON to string
  String json;
  serializeJson(array, json);
  // Send the JSON response
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
}

/**
 * @brief Handle WiFi network scan
 * Returns a list of available WiFi networks as JSON
 * This function scans for available WiFi networks and returns them along with
 * the list of already configured networks
 */
void handleWiFiScan() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  DynamicJsonDocument doc(2048);
  // Scan for available networks
  int n = WiFi.scanNetworks();
  yield();
  // Add available networks
  JsonArray networks = doc.createNestedArray("networks");
  for (int i = 0; i < n; ++i) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    // Yield to other tasks during long operations
    yield();
  }
  // Add configured networks
  JsonArray configured = doc.createNestedArray("configured");
  for (int i = 0; i < wifiNetworkCount; i++) {
    configured.add(String(ssid[i]));
    // Yield to other tasks during long operations
    yield();
  }
  // Serialize JSON to string
  String json;
  serializeJson(doc, json);
  // Send the JSON response
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
}

/**
 * @brief Handle WiFi configuration save
 * Saves WiFi credentials to SPIFFS
 * This function receives WiFi credentials via HTTP POST and saves them to wifi.json
 * It supports both single network and multiple network configurations
 */
void handleWiFiSave() {
  if (!server.hasArg("plain")) {
    sendJsonResponse("error", "Missing JSON data");
    return;
  }
  // Parse JSON data
  String json = server.arg("plain");
  DynamicJsonDocument doc(2048);  // Increased size for array format
  DeserializationError error = deserializeJson(doc, json);
  // Check for errors
  if (error) {
    sendJsonResponse("error", "Invalid JSON");
    return;
  }
  // Handle the new JSON array format [{"ssid": "name", "password": "pass"}, ...]
  wifiNetworkCount = 0;
  if (doc.is<JsonArray>()) {
    JsonArray networks = doc.as<JsonArray>();
    for (JsonObject network : networks) {
      if (wifiNetworkCount >= MAX_WIFI_NETWORKS) break;
      // Handle required SSID
      if (network.containsKey("ssid")) {
        const char* ssidValue = network["ssid"];
        if (ssidValue && strlen(ssidValue) > 0 && strlen(ssidValue) < sizeof(ssid[wifiNetworkCount])) {
          strncpy(ssid[wifiNetworkCount], ssidValue, sizeof(ssid[wifiNetworkCount]) - 1);
          ssid[wifiNetworkCount][sizeof(ssid[wifiNetworkCount]) - 1] = '\0';
        } else {
          sendJsonResponse("error", "Invalid SSID");
          return;
        }
        // Handle optional password
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
        // Increment network count
        wifiNetworkCount++;
      }
    }
  }
  // Save updated credentials to SPIFFS
  saveWiFiCredentials();
  sendJsonResponse("success", "WiFi configuration saved");
}

/**
 * @brief Handle WiFi status request
 * Returns the current WiFi connection status as JSON
 * This function provides information about the current WiFi connection including
 * connection status, SSID, IP address, and signal strength
 */
void handleWiFiStatus() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  DynamicJsonDocument doc(256);
  // Add connection status
  if (WiFi.status() == WL_CONNECTED) {
    doc["connected"] = true;
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  } else {
    doc["connected"] = false;
  }
  // Serialize JSON to string
  String json;
  serializeJson(doc, json);
  // Send the JSON response
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
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
    config.display_type = 0;
    config.display_address = DEFAULT_DISPLAY_ADDR;
    config.display_timeout = 30;
    // Save the default configuration to file
    saveConfig();
  } else {
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
    config.display_type = doc.containsKey("display_type") ? doc["display_type"] : 0;
    config.display_address = doc.containsKey("display_address") ? doc["display_address"] : DEFAULT_DISPLAY_ADDR;
    config.display_timeout = doc.containsKey("display_timeout") ? doc["display_timeout"] : 30;
    // Print loaded configuration
    Serial.println("Loaded configuration from SPIFFS");
  }
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
  doc["display_type"] = config.display_type;
  doc["display_address"] = config.display_address;
  doc["display_timeout"] = config.display_timeout;
  doc["touch_play"] = config.touch_play;
  doc["touch_next"] = config.touch_next;
  doc["touch_prev"] = config.touch_prev;
  // Save the JSON document to SPIFFS using helper function
  if (writeJsonFile("/config.json", doc)) {
    Serial.println("Saved configuration to SPIFFS");
  } else {
    Serial.println("Failed to save configuration to SPIFFS");
  }
}

/**
 * @brief Handle GET request for configuration
 * Returns the current configuration as JSON
 * This function serves the current configuration in JSON format.
 */
void handleGetConfig() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  DynamicJsonDocument doc(1024);
  // Populate JSON document with configuration values
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
  doc["display_type"] = config.display_type;
  doc["display_address"] = config.display_address;
  doc["display_timeout"] = config.display_timeout;
  doc["touch_play"] = config.touch_play;
  doc["touch_next"] = config.touch_next;
  doc["touch_prev"] = config.touch_prev;
  doc["touch_threshold"] = config.touch_threshold;
  doc["touch_debounce"] = config.touch_debounce;
  // Add display types information
  JsonArray displays = doc.createNestedArray("displays");
  for (int i = 0; i < getDisplayTypeCount(); i++) {
    const char* displayName = getDisplayTypeName(i);
    if (displayName) {
      displays.add(displayName);
    }
  }
  // Serialize JSON to string
  String json;
  serializeJson(doc, json);
  // Return configuration as JSON
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
}

/**
 * @brief Handle POST request for configuration
 * Updates the configuration with new JSON data and saves to SPIFFS
 * This function receives a new configuration via HTTP POST, validates it, and saves it to SPIFFS.
 */
void handlePostConfig() {
  // Check for JSON data
  if (!server.hasArg("plain")) {
    sendJsonResponse("error", "Missing JSON data");
    return;
  }
  // Parse the JSON data
  String jsonData = server.arg("plain");
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonData);
  // Check for JSON parsing errors
  if (error) {
    sendJsonResponse("error", "Invalid JSON");
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
  if (doc.containsKey("display_type")) config.display_type = doc["display_type"];
  if (doc.containsKey("display_address")) config.display_address = doc["display_address"];
  if (doc.containsKey("display_timeout")) config.display_timeout = doc["display_timeout"];
  if (doc.containsKey("touch_play")) config.touch_play = doc["touch_play"];
  if (doc.containsKey("touch_next")) config.touch_next = doc["touch_next"];
  if (doc.containsKey("touch_prev")) config.touch_prev = doc["touch_prev"];
  if (doc.containsKey("touch_threshold")) config.touch_threshold = doc["touch_threshold"];
  if (doc.containsKey("touch_debounce")) config.touch_debounce = doc["touch_debounce"];
  // Save to SPIFFS
  saveConfig();
  // Return status as JSON
  sendJsonResponse("success", "Configuration updated successfully");
}



/**
 * @brief Audio task function
 * Handles audio streaming on core 0
 * @param pvParameters Task parameters (not used)
 */
void audioTask(void *pvParameters) {
  while (true) {
    // Process audio streaming with error handling
    player.handleAudio();
    // Very small delay to prevent busy waiting but allow frequent processing
    delay(1);
  }
}



/**
 * @brief Interrupt service routine for board button
 * Sets a flag when the board button is pressed
 */
void boardButtonISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  // Debounce the button press (ignore if less than 50ms since last press)
  if (interruptTime - lastInterruptTime > 50) {
    boardButtonPressed = true;  // Set flag to indicate button press detected
  }
  lastInterruptTime = interruptTime;  // Update last interrupt time for debouncing
}

/**
 * @brief Handle board button input
 * Processes the built-in button (GPIO 0) for play/stop toggle functionality
 * This function checks for button presses detected by the interrupt handler
 */
void handleBoardButton() {
  // Only handle board button if it's configured (not negative)
  if (config.board_button < 0) {
    return;
  }
  // Check if button was pressed (detected by interrupt)
  if (boardButtonPressed) {
    // Toggle play/stop
    if (player.isPlaying()) {
      // If currently playing, stop the stream
      player.stopStream();
    } else {
      // If we have a current stream, resume it
      if (strlen(player.getStreamUrl()) > 0) {
        player.startStream();
      } 
      // Otherwise, if we have playlist items, play the selected one
      else if (player.isPlaylistIndexValid()) {
        player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
      }
      // Save player state after starting
      player.savePlayerState();
    }
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Clear the flag
    boardButtonPressed = false;
  }
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
    // Process the rotation
    if (diff > 0) {
      // Rotate clockwise - volume up or next item
      if (player.isPlaying()) {
        // If playing, increase volume by 1 (capped at 22)
        player.setVolume(min(22, player.getVolume() + 1));
        sendStatusToClients();  // Notify clients of status change
      } else {
        // If not playing, select next item in playlist
        player.setPlaylistIndex(player.getNextPlaylistItem());
      }
    } else if (diff < 0) {
      // Rotate counter-clockwise - volume down or previous item
      if (player.isPlaying()) {
        // If playing, decrease volume by 1 (capped at 0)
        player.setVolume(max(0, player.getVolume() - 1));
        sendStatusToClients();  // Notify clients of status change
      } else {
        // If not playing, select previous item in playlist
        player.setPlaylistIndex(player.getPrevPlaylistItem());
      }
    }
    // Update last position
    lastRotaryPosition = currentPosition;  
    // Update activity time on user interaction
    display->setActivityTime(millis()); 
    // Refresh display with new values
    updateDisplay();                      
  }
  // Process button press if detected
  if (rotaryEncoder.wasButtonPressed()) {
    // Update activity time
    display->setActivityTime(millis()); 
    if (player.isPlaying()) {
      // If playing, stop playback
      player.stopStream();
    } else {
      // If we have a current stream, resume it
      if (strlen(player.getStreamUrl()) > 0) {
        player.startStream();
      } 
      // Otherwise, if we have playlist items, play the selected one
      else if (player.isPlaylistIndexValid()) {
        player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
      }
      // Save state when user initiates playback
      player.savePlayerState(); 
    }
    // Refresh display
    updateDisplay();
    // Notify clients of status change 
    sendStatusToClients();
  }
}

/**
 * @brief Handle touch button input
 * Processes the touch buttons for play/pause, next/volume-up, and previous/volume-down
 * This function implements the same functionality as the rotary encoder
 */
void handleTouch() {
  // Handle play/pause button
  if (touchPlay && touchPlay->wasPressed()) {
    display->setActivityTime(millis()); // Update activity time
    updateDisplay(); // Turn display back on and update
    // Toggle play/stop
    if (player.isPlaying()) {
      // If playing, stop playback
      player.stopStream();
    } else {
      // If we have a current stream, resume it
      if (strlen(player.getStreamUrl()) > 0) {
        player.startStream();
      } 
      // Otherwise, if we have playlist items, play the selected one
      else if (player.isPlaylistIndexValid()) {
        player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
      }
      // Save state when user initiates playback
      player.savePlayerState(); 
    }
    // Refresh display and notify clients of status change
    updateDisplay();
    sendStatusToClients();
  }
  // Handle next/volume-up button
  if (touchNext && touchNext->wasPressed()) {
    display->setActivityTime(millis()); // Update activity time
    updateDisplay(); // Turn display back on and update
    if (player.isPlaying()) {
      // If playing, increase volume by 1 (capped at 22)
      player.setVolume(min(22, player.getVolume() + 1));
    } else {
      // If not playing, select next item in playlist
      player.setPlaylistIndex(player.getNextPlaylistItem());
    }
    // Update display and notify clients of status change
    updateDisplay();
    sendStatusToClients();
  }
  // Handle previous/volume-down button
  if (touchPrev && touchPrev->wasPressed()) {
    display->setActivityTime(millis()); // Update activity time
    updateDisplay(); // Turn display back on and update
    if (player.isPlaying()) {
      // If playing, decrease volume by 1 (capped at 0)
      player.setVolume(max(0, player.getVolume() - 1));
      sendStatusToClients();  // Notify clients of status change
    } else {
      // If not playing, select previous item in playlist
      player.setPlaylistIndex(player.getPrevPlaylistItem());
    }
    // Refresh display
    updateDisplay();  
  }
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
        if (server.hasArg("stream") && player.getPlaylistCount() > 0) {
          int streamIndex = server.arg("stream").toInt();
          if (streamIndex >= 0 && streamIndex < player.getPlaylistCount()) {
            // Stop playback
            player.stopStream();
            // Play selected stream
            player.setPlaylistIndex(streamIndex);
            player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
          }
        } else if (strlen(player.getStreamUrl()) > 0) {
          // Stop current playback
          player.stopStream();
          // Resume current stream
          player.startStream();
        } else if (player.isPlaylistIndexValid()) {
          // Stop playback
          player.stopStream();
          // Play currently selected stream
          player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
        }
        // Save player state when user requests to play
        player.savePlayerState();
      } else if (action == "stop") {
        // Stop playback
        player.stopStream();
      } else if (action == "volume") {
        // Set volume
        if (server.hasArg("volume")) {
          int newVolume = server.arg("volume").toInt();
          if (newVolume >= 0 && newVolume <= 22) {
            player.setVolume(newVolume);
            // Update display and notify clients
            updateDisplay();
            sendStatusToClients();
          }
        }
      } else if (action == "instant") {
        // Play a stream URL
        if (server.hasArg("url")) {
          String customUrl = server.arg("url");
          if (customUrl.length() > 0 && 
              (customUrl.startsWith("http://") || customUrl.startsWith("https://"))) {
            // Stop current playback
            player.stopStream();
            // Use a generic name for the stream
            String streamName = "Stream";
            player.startStream(customUrl.c_str(), streamName.c_str());
          }
        }
      }
    }
  }
  // Create a more memory-efficient HTML response
  String html = "<!DOCTYPE html><html><head><title>NetTuner</title>";
  html += "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@picocss/pico@2/css/pico.classless.min.css\">";
  html += "</head><body><header><h1>NetTuner</h1></header><main>";
  html += "<section><h2>Status: ";
  html += player.isPlaying() ? "PLAY" : "STOP";
  html += "</h2>";
  
  // Show current stream name
  if (player.isPlaying() && player.getStreamTitle()[0]) {
    html += "<p><b>Now playing:</b> ";
    html += player.getStreamTitle();
    html += "</p>";
  } else if (!player.isPlaying() && player.getPlaylistCount() > 0 && player.getPlaylistIndex() < player.getPlaylistCount()) {
    html += "<p><b>Selected:</b> ";
    html += player.getPlaylistItem(player.getPlaylistIndex()).name;
    html += "</p>";
  }
  html += "</section><section><h2>Controls</h2>";
  html += "<form method='post'><fieldset role='group'>";
  html += "<button name='action' value='play' type='submit'>Play</button> ";
  html += "<button name='action' value='stop' type='submit'>Stop</button>";
  html += "</fieldset></form>";
  html += "<form method='post'><fieldset role='group'>";
  html += "<select name='volume' id='volume'>";
  
  // Add volume options (0-22)
  for (int i = 0; i <= 22; i++) {
    html += "<option value='" + String(i) + "'";
    if (i == player.getVolume()) {
      html += " selected";
    }
    html += ">" + String(i) + "</option>";
  }
  html += "</select>";
  html += "<button name='action' value='volume' type='submit'>Set&nbsp;volume</button>";
  html += "</fieldset></form></section><section><h2>Playlist</h2>";
  
  // Show stream selection dropdown if we have a playlist
  if (player.getPlaylistCount() > 0) {
    html += "<form method='post'><fieldset role='group'>";
    html += "<select name='stream' id='stream'>";
    
    // Populate the dropdown with available streams
    for (int i = 0; i < player.getPlaylistCount(); i++) {
      html += "<option value='" + String(i) + "'";
      if (i == player.getPlaylistIndex()) {
        html += " selected";
      }
      html += ">" + String(player.getPlaylistItem(i).name) + "</option>";
    }
    html += "</select>";
    html += "<button name='action' value='play' type='submit'>Play&nbsp;selected</button>";
    html += "</fieldset></form>";
  } else {
    html += "<p>No streams in playlist.</p>";
  }
  
  // Add instant play input for custom stream URL even when no playlist
  html += "<h2>Play instant stream</h2>";
  html += "<form method='post'><fieldset role='group'>";
  html += "<input type='url' name='url' id='url' placeholder='http://example.com/stream'>";
  html += "<button name='action' value='instant' type='submit'>Play&nbsp;stream</button>";
  html += "</fieldset></form></section></main>";
  html += "<footer><p>NetTuner Simple Interface</p></footer></body></html>";
  
  // Send the HTML response
  server.send(200, "text/html", html);
}


/**
 * @brief Handle GET request for streams
 * Returns the current playlist as JSON
 * This function serves the current playlist in JSON format. If the playlist file
 * doesn't exist, it creates a default empty one.
 */
void handleGetStreams() {
  // Yield to other tasks before processing
  yield();
  // If playlist file doesn't exist, return a default empty one
  if (!SPIFFS.exists("/playlist.json")) {
    server.send(200, "application/json", "[]");
    return;
  }
  File file = SPIFFS.open("/playlist.json", "r");
  if (!file) {
    server.send(200, "application/json", "[]");
    return;
  }
  // Stream the file contents
  server.streamFile(file, "application/json");
  file.close();
  // Yield to other tasks after processing
  yield();
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
    sendJsonResponse("error", "Missing JSON data");
    return;
  }
  // Parse the JSON data
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, jsonData);
  // Check for JSON parsing errors
  if (error) {
    Serial.print("JSON parsing error: ");
    Serial.println(error.c_str());
    sendJsonResponse("error", "Invalid JSON format");
    return;
  }
  // Ensure it's an array
  if (!doc.is<JsonArray>()) {
    sendJsonResponse("error", "JSON root must be an array");
    return;
  }
  // Get the array
  JsonArray array = doc.as<JsonArray>();
  // Validate array size
  if (array.size() > MAX_PLAYLIST_SIZE) {
    sendJsonResponse("error", "Playlist exceeds maximum size");
    return;
  }
  // Clear existing playlist
  player.clearPlaylist();
  // Process each item in the array
  for (JsonObject item : array) {
    // Validate required fields
    if (!item.containsKey("name") || !item.containsKey("url")) {
      sendJsonResponse("error", "Each item must have 'name' and 'url' fields");
      return;
    }
    // Extract name and url
    const char* name = item["name"];
    const char* url = item["url"];
    // Validate data
    if (!name || !url || strlen(name) == 0 || strlen(url) == 0) {
      sendJsonResponse("error", "Name and URL cannot be empty");
      return;
    }
    // Validate URL format
    if (!VALIDATE_URL(url)) {
      sendJsonResponse("error", "Invalid URL format");
      return;
    }
    // Add to playlist
    player.addPlaylistItem(name, url);
  }
  // Save to SPIFFS
  player.savePlaylist();
  // Send success response
  sendJsonResponse("success", "Playlist updated successfully");
}

/**
 * @brief Handle player request
 * Controls stream playback (play/stop) or returns player status
 * This function handles HTTP requests to control playback or get player status.
 * For POST requests, it supports both JSON payload and form data with action parameter.
 * For GET requests, it returns player status and stream information.
 * 
 * POST /api/player:
 *   JSON payload: {"action": "play", "url": "...", "name": "...", "index": 0}
 *   JSON payload: {"action": "play", "index": 0}
 *   JSON payload: {"action": "stop"}
 *   Form data: action=play&url=...&name=...&index=0
 *   Form data: action=play&index=0
 *   Form data: action=stop
 * 
 * GET /api/player:
 *   Returns: {"status": "play|stop", "stream": {...}}
 *   When playing: stream object contains name, title, url, index, bitrate, elapsed
 *   When stopped: stream object is omitted
 */
void handlePlayer() {
  // Handle GET request - return player status
  if (server.method() == HTTP_GET) {
    // Create JSON document with appropriate size
    DynamicJsonDocument doc(512);
    // Add player status
    doc["status"] = player.isPlaying() ? "play" : "stop";
    // If playing, add stream information
    if (player.isPlaying()) {
      JsonObject streamObj = doc.createNestedObject("stream");
      streamObj["name"] = player.getStreamName();
      streamObj["title"] = player.getStreamTitle();
      streamObj["url"] = player.getStreamUrl();
      streamObj["index"] = player.getPlaylistIndex();
      streamObj["bitrate"] = player.getBitrate();
      // Calculate elapsed time
      if (player.getPlayStartTime() > 0) {
        unsigned long currentTime = millis() / 1000;
        unsigned long elapsedTime = currentTime - player.getPlayStartTime();
        streamObj["elapsed"] = elapsedTime;
      } else {
        streamObj["elapsed"] = 0;
      }
    }
    // Serialize JSON to string
    String json;
    serializeJson(doc, json);
    // Return status as JSON
    server.send(200, "application/json", json);
    return;
  }
  // Handle POST request - control playback
  String action, url, name;
  int index = -1;
  // Check if request has JSON payload
  if (server.hasArg("plain")) {
    // Handle JSON payload
    String json = server.arg("plain");
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json);
    // Check for JSON parsing errors
    if (error) {
      sendJsonResponse("error", "Invalid JSON");
      return;
    }
    // Extract parameters from JSON
    if (doc.containsKey("action")) {
      action = doc["action"].as<String>();
    }
    if (doc.containsKey("url")) {
      url = doc["url"].as<String>();
    }
    if (doc.containsKey("name")) {
      name = doc["name"].as<String>();
    }
    if (doc.containsKey("index")) {
      index = doc["index"].as<int>();
    }
  } 
  // Check if request has form data
  else if (server.hasArg("action")) {
    // Handle form data
    action = server.arg("action");
    if (server.hasArg("url")) {
      url = server.arg("url");
    }
    if (server.hasArg("name")) {
      name = server.arg("name");
    }
    if (server.hasArg("index")) {
      index = server.arg("index").toInt();
    }
  } 
  else {
    sendJsonResponse("error", "Missing action parameter");
    return;
  }
  // Check for required action parameter
  if (action.length() == 0) {
    sendJsonResponse("error", "Missing required parameter: action");
    return;
  }
  if (action == "play") {
    // Handle case where only index is provided
    if (url.length() == 0 && name.length() == 0 && index >= 0) {
      // Validate index
      if (index >= player.getPlaylistCount()) {
        sendJsonResponse("error", "Invalid playlist index");
        return;
      }
      // Extract stream data from playlist
      url = String(player.getPlaylistItem(index).url);
      name = String(player.getPlaylistItem(index).name);
      player.setPlaylistIndex(index);
    } 
    // Handle case where URL is provided (with optional name)
    else if (url.length() > 0) {
      // If no name provided, check if we have a current stream name
      if (name.length() == 0 && strlen(player.getStreamUrl()) > 0 && url == String(player.getStreamUrl())) {
        name = (strlen(player.getStreamName()) > 0) ? String(player.getStreamName()) : "Unknown Station";
      }
      // Validate URL format
      if (!url.startsWith("http://") && !url.startsWith("https://")) {
        sendJsonResponse("error", "Invalid URL format. Must start with http:// or https://");
        return;
      }
      // Update currentSelection based on URL
      for (int i = 0; i < player.getPlaylistCount(); i++) {
        if (strcmp(player.getPlaylistItem(i).url, url.c_str()) == 0) {
          player.setPlaylistIndex(i);
          break;
        }
      }
    }
    // Handle case where we're resuming playback
    else if (url.length() == 0 && strlen(player.getStreamUrl()) > 0) {
      url = String(player.getStreamUrl());
      name = (strlen(player.getStreamName()) > 0) ? String(player.getStreamName()) : "Unknown Station";
    }
    // No valid play parameters
    else {
      sendJsonResponse("error", "Missing required parameters for play action");
      return;
    }
    // Stop any currently playing stream
    player.stopStream();
    // Start the stream
    player.startStream(url.c_str(), name.c_str());
    // Save player state when user requests to play
    player.savePlayerState();
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Send success response
    sendJsonResponse("success", "Stream started successfully");
  } 
  else if (action == "stop") {
    // Stop any currently playing stream
    player.stopStream();
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Send success response
    sendJsonResponse("success", "Stream stopped successfully");
  } 
  else {
    sendJsonResponse("error", "Invalid action. Supported actions: play, stop");
    return;
  }
}

/**
 * @brief Handle mixer request
 * Gets or sets the volume and tone levels
 * This function handles HTTP requests to get or set the volume and/or tone levels. 
 * For GET requests, it returns the current mixer status as JSON.
 * For POST requests, it supports both JSON payload and form data, validates the input, and updates the settings.
 * 
 * GET /api/mixer:
 *   Returns: {"volume": 11, "bass": 0, "mid": 0, "treble": 0}
 * 
 * POST /api/mixer:
 *   JSON payload: {"volume": 10}
 *   JSON payload: {"bass": 4, "treble": -2}
 *   Form data: volume=10
 *   Form data: bass=4&treble=-2
 */
void handleMixer() {
  // Handle GET request - return current mixer status
  if (server.method() == HTTP_GET) {
    // Create JSON document with appropriate size
    DynamicJsonDocument doc(256);
    // Add mixer status
    doc["volume"] = player.getVolume();
    doc["bass"] = player.getBass();
    doc["mid"] = player.getMid();
    doc["treble"] = player.getTreble();
    // Serialize JSON to string
    String json;
    serializeJson(doc, json);
    // Return status as JSON
    server.send(200, "application/json", json);
    return;
  }
  // Handle POST request - update mixer settings
  DynamicJsonDocument doc(256);
  bool hasData = false;
  // Handle JSON payload
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    DeserializationError error = deserializeJson(doc, json);
    // Check for JSON parsing errors
    if (error) {
      sendJsonResponse("error", "Invalid JSON");
      return;
    }
    hasData = true;
  }
  // Handle form data
  else {
    // Check if any form parameters are present
    if (server.hasArg("volume") || server.hasArg("bass") || 
        server.hasArg("mid") || server.hasArg("treble")) {
      hasData = true;
      // Add form data to JSON document
      if (server.hasArg("volume")) {
        doc["volume"] = server.arg("volume");
      }
      if (server.hasArg("bass")) {
        doc["bass"] = server.arg("bass");
      }
      if (server.hasArg("mid")) {
        doc["mid"] = server.arg("mid");
      }
      if (server.hasArg("treble")) {
        doc["treble"] = server.arg("treble");
      }
    }
  }
  // Check if any data was provided
  if (!hasData) {
    sendJsonResponse("error", "Missing data: volume, bass, mid, or treble");
    return;
  }
  bool toneUpdated = false;
  // Handle volume setting
  if (doc.containsKey("volume")) {
    int newVolume;
    if (doc["volume"].is<const char*>()) {
      newVolume = atoi(doc["volume"].as<const char*>());
    } else {
      newVolume = doc["volume"];
    }
    // Validate volume range
    if (newVolume < 0 || newVolume > 22) {
      sendJsonResponse("error", "Volume must be between 0 and 22");
      return;
    }
    player.setVolume(newVolume);
  }
  // Handle bass setting
  if (doc.containsKey("bass")) {
    int newBass;
    if (doc["bass"].is<const char*>()) {
      newBass = atoi(doc["bass"].as<const char*>());
    } else {
      newBass = doc["bass"];
    }
    if (newBass < -6 || newBass > 6) {
      sendJsonResponse("error", "Bass must be between -6 and 6");
      return;
    }
    player.setBass(newBass);
    toneUpdated = true;
  }
  // Handle mid setting
  if (doc.containsKey("mid")) {
    int newMid;
    if (doc["mid"].is<const char*>()) {
      newMid = atoi(doc["mid"].as<const char*>());
    } else {
      newMid = doc["mid"];
    }
    if (newMid < -6 || newMid > 6) {
      sendJsonResponse("error", "Midrange must be between -6 and 6");
      return;
    }
    player.setMid(newMid);
    toneUpdated = true;
  }
  // Handle treble setting
  if (doc.containsKey("treble")) {
    int newTreble;
    if (doc["treble"].is<const char*>()) {
      newTreble = atoi(doc["treble"].as<const char*>());
    } else {
      newTreble = doc["treble"];
    }
    if (newTreble < -6 || newTreble > 6) {
      sendJsonResponse("error", "Treble must be between -6 and 6");
      return;
    }
    player.setTreble(newTreble);
    toneUpdated = true;
  }
  // Apply tone settings to audio
  if (toneUpdated) {
    player.setTone();
  }
  // Update display and notify clients
  updateDisplay();
  sendStatusToClients();
  // Send success response
  sendJsonResponse("success", "Mixer settings updated successfully");
}


/**
 * @brief Handle export configuration request
 * Exports all JSON configuration files from SPIFFS as a single JSON object
 * This function reads all JSON files from SPIFFS and combines them into a single
 * JSON object where keys are filenames and values are file contents.
 */
void handleExportConfig() {
  // Yield to other tasks before processing
  yield();
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
              // Yield to other tasks during long operations
              yield(); 
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
  // Yield to other tasks after processing
  delay(1);
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
    sendJsonResponse("error", "Method not allowed", 405);
    return;
  }
  // Check if we have data in the request body
  if (!server.hasArg("plain")) {
    sendJsonResponse("error", "No data received");
    return;
  }
  // Get the JSON data from the request body
  String jsonData = server.arg("plain");
  // Check if data is empty
  if (jsonData.length() == 0) {
    sendJsonResponse("error", "No file uploaded");
    return;
  }
  // Parse the JSON data
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.printf("Failed to parse uploaded JSON: %s\n", error.c_str());
    sendJsonResponse("error", "Invalid JSON format");
    return;
  }
  // Process each configuration section
  bool success = true;
  const char* configFiles[] = {"config.json", "wifi.json", "playlist.json", "player.json"};
  for (int i = 0; i < 4; i++) {
    const char* filename = configFiles[i];
    // Check if this section exists in the uploaded data
    if (doc.containsKey(filename)) {
      String file = String("/") + String(filename);
      // Create a temporary DynamicJsonDocument from the JsonVariant
      // TODO: Optimize size based on actual content
      // TODO: needs testing
      DynamicJsonDocument tempDoc(1024);
      tempDoc.set(doc[filename]);
      // Save the JSON to SPIFFS using helper function
      if (writeJsonFile(file.c_str(), tempDoc)) {
        Serial.println("Saved " + String(filename) + " to SPIFFS");
      } else {
        Serial.println("Failed to save " + String(filename) + " to SPIFFS");
      }
      // Yield to other tasks during long operations
      delay(1);
    }
  }
  if (success) {
    sendJsonResponse("success", "Configuration imported successfully");
  } else {
    sendJsonResponse("error", "Error importing configuration", 500);
  }
}


/**
 * @brief Generate JSON status string
 * Creates a JSON string with current player status information
 * @param fullStatus If true, generates full status; if false, generates partial status
 * @return JSON formatted status string
 */
String generateStatusJSON(bool fullStatus) {
  // Create JSON document with appropriate size
  DynamicJsonDocument doc(512);
  if (fullStatus) {
    // Populate JSON document with all status values
    doc["playing"] = player.isPlaying();
    doc["streamURL"] = player.getStreamUrl();
    doc["streamName"] = player.getStreamName();
    doc["streamTitle"] = player.getStreamTitle();
    doc["streamIcyURL"] = player.getStreamIcyUrl();
    doc["streamIconURL"] = player.getStreamIconUrl();
    doc["bitrate"] = player.getBitrate();
    doc["volume"] = player.getVolume();
    doc["bass"] = player.getBass();
    doc["mid"] = player.getMid();
    doc["treble"] = player.getTreble();
  } else {
    // Only include the bitrate in partial status
    doc["bitrate"] = player.getBitrate();
  }
  // Serialize JSON to string
  String json;
  serializeJson(doc, json);
  return json;
}

/**
 * @brief Send status to all connected WebSocket clients
 * This function broadcasts the current player status to all connected WebSocket clients.
 * The status includes playback state, stream information, bitrate, and volume.
 * It only sends the status if it has changed from the previous status.
 */
void sendStatusToClients(bool fullStatus) {
  // Only broadcast if WebSocket server has clients AND they are connected
  if (webSocket.connectedClients() > 0) {
    String status = generateStatusJSON(fullStatus);
    // Only send if status has changed
    if (status != previousStatus) {
      // Use broadcastTXT with error handling
      webSocket.broadcastTXT(status);
      // Update previous status
      previousStatus = status;
    }
  }
}


/**
 * @brief Handle HTTP proxy requests
 * Acts as a transparent proxy to circumvent CORS restrictions
 * Forwards requests to target URLs and returns responses
 */
void handleProxyRequest() {
  // Check if we have a URL parameter
  if (!server.hasArg("url")) {
    sendJsonResponse("error", "Missing URL parameter", 400);
    return;
  }
  // Get the target URL
  String targetUrl = server.arg("url");
  // Validate URL format
  if (!targetUrl.startsWith("http://") && !targetUrl.startsWith("https://")) {
    sendJsonResponse("error", "Invalid URL format. Must start with http:// or https://", 400);
    return;
  }
  // Create HTTP client
  HTTPClient http;
  // Set timeouts to prevent hanging
  http.setTimeout(5000);
  // Configure the request based on the original method
  http.begin(targetUrl);
  // Copy headers from the original request
  int headerCount = server.headers();
  for (int i = 0; i < headerCount; i++) {
    String headerName = server.headerName(i);
    String headerValue = server.header(i);
    // Skip headers that shouldn't be forwarded
    if (headerName.equalsIgnoreCase("Host") || 
        headerName.equalsIgnoreCase("Connection") ||
        headerName.equalsIgnoreCase("Content-Length")) {
      continue;
    }
    // Forward all other headers
    http.addHeader(headerName, headerValue);
  }
  // Variable to hold HTTP response code
  int httpResponseCode;
  // Handle different HTTP methods
  if (server.method() == HTTP_GET) {
    httpResponseCode = http.GET();
  } else if (server.method() == HTTP_HEAD) {
    httpResponseCode = http.GET(); // Use GET but we'll only send headers
  } else if (server.method() == HTTP_POST) {
    // Get request body if present
    String requestBody = server.arg("plain");
    httpResponseCode = http.POST(requestBody);
  } else {
    http.end();
    sendJsonResponse("error", "Unsupported HTTP method", 405);
    return;
  }
  // Check for HTTP errors
  if (httpResponseCode > 0) {
    // Get response headers and forward them
    // Use public methods to iterate through headers
    int headerCount = 0;
    String headerName, headerValue;
    // Get all headers one by one until we've processed them all
    while (true) {
      headerName = http.headerName(headerCount);
      headerValue = http.header(headerCount);
      // If we get empty strings, we've reached the end of headers
      if (headerName.length() == 0 && headerValue.length() == 0) {
        break;
      }
      // Skip headers that might cause issues
      if (!headerName.equalsIgnoreCase("Connection") &&
          !headerName.equalsIgnoreCase("Transfer-Encoding")) {
        server.sendHeader(headerName, headerValue, false);
      }
      // Increment header count
      headerCount++;
      // Safety check to prevent infinite loop
      if (headerCount > 100) {
        break;
      }
    }
    // For HEAD requests, only send headers without content
    if (server.method() == HTTP_HEAD) {
      // Get content type
      String contentType = http.header("Content-Type");
      if (contentType.isEmpty()) {
        // Try to determine content type from URL
        if (targetUrl.endsWith(".png") || targetUrl.endsWith(".PNG")) {
          contentType = "image/png";
        } else if (targetUrl.endsWith(".jpg") || targetUrl.endsWith(".jpeg") || 
                   targetUrl.endsWith(".JPG") || targetUrl.endsWith(".JPEG")) {
          contentType = "image/jpeg";
        } else if (targetUrl.endsWith(".gif") || targetUrl.endsWith(".GIF")) {
          contentType = "image/gif";
        } else {
          contentType = "application/octet-stream";
        }
      }
      // Send response with proper content type and length (no content body for HEAD)
      server.setContentLength(http.getSize());
      server.send(httpResponseCode, contentType, "");
    } else {
      // For GET requests, stream the response directly to client
      WiFiClient * stream = http.getStreamPtr();
      // Get content type
      String contentType = http.header("Content-Type");
      if (contentType.isEmpty()) {
        // Try to determine content type from URL
        if (targetUrl.endsWith(".png") || targetUrl.endsWith(".PNG")) {
          contentType = "image/png";
        } else if (targetUrl.endsWith(".jpg") || targetUrl.endsWith(".jpeg") || 
                   targetUrl.endsWith(".JPG") || targetUrl.endsWith(".JPEG")) {
          contentType = "image/jpeg";
        } else if (targetUrl.endsWith(".gif") || targetUrl.endsWith(".GIF")) {
          contentType = "image/gif";
        } else {
          contentType = "application/octet-stream";
        }
      }
      // Send response with proper content type and length
      server.setContentLength(http.getSize());
      server.send(httpResponseCode, contentType, "");
      // Stream the content
      const size_t bufferSize = 1024;
      uint8_t buffer[bufferSize];
      size_t totalBytesRead = 0;
      size_t contentLength = http.getSize();
      // Read and send data in chunks
      while (http.connected() && (contentLength == 0 || totalBytesRead < contentLength)) {
        size_t bytesAvailable = stream->available();
        if (bytesAvailable) {
          size_t bytesRead = stream->readBytes(buffer, min(bytesAvailable, bufferSize));
          server.client().write(buffer, bytesRead);
          totalBytesRead += bytesRead;
        }
        // Yield to other tasks
        yield();
      }
    }
  } else {
    // HTTP error occurred
    http.end();
    Serial.printf("HTTP request failed: %s\n", http.errorToString(httpResponseCode).c_str());
    sendJsonResponse("error", "Proxy request failed: " + String(http.errorToString(httpResponseCode)), 500);
    return;
  }
  // End the HTTP connection
  http.end();
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
      {
        // Add a small delay before sending to ensure connection is established
        delay(10);
        String status = generateStatusJSON(true);
        // Send status to newly connected client with error checking
        if (webSocket.clientIsConnected(num)) {
            webSocket.sendTXT(num, status);
        }
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
  // Check if display is initialized
  if (display == nullptr) return;
  // Get current IP address or "No IP" if not connected
  String ipString;
  if (WiFi.status() == WL_CONNECTED) {
    ipString = WiFi.localIP().toString();
  } else {
    ipString = "No IP";
  }
  // When not playing, show the selected playlist item name instead of empty stream name
  const char* displayStreamName = player.getStreamName();
  if (!player.isPlaying() && strlen(displayStreamName) == 0) {
    // If we have a playlist and a valid index, show the selected item name
    if (player.getPlaylistCount() > 0 && player.getPlaylistIndex() < player.getPlaylistCount()) {
      displayStreamName = player.getPlaylistItem(player.getPlaylistIndex()).name;
    }
  }
  // Update the display with current status
  display->update(player.isPlaying(), player.getStreamTitle(), displayStreamName, player.getVolume(), player.getBitrate(), ipString);
}


/**
 * @brief Initialize SPIFFS with error recovery
 * Mounts SPIFFS filesystem with error recovery mechanisms
 * @return true if successful, false otherwise
 */
bool initSPIFFS() {
  // Initialize SPIFFS with error recovery
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    // Try to reformat SPIFFS
    if (!SPIFFS.format()) {
      Serial.println("ERROR: Failed to format SPIFFS");
      return false;
    }
    // Try to mount again after formatting
    if (!SPIFFS.begin(true)) {
      Serial.println("ERROR: Failed to mount SPIFFS after formatting");
      return false;
    }
    Serial.println("SPIFFS formatted and mounted successfully");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }
  // Test SPIFFS write capability
  if (!SPIFFS.exists("/spiffs_test")) {
    // File does not exist, create it
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
    // File exists, SPIFFS is working
    Serial.println("SPIFFS write test file already exists - SPIFFS is working");
  }
  // End the SPIFFS test
  return true;
}


/**
 * @brief Setup web server routes and static file serving
 * Configures all HTTP routes and static file mappings for the web server
 */
void setupWebServer() {
  server.on("/api/streams", HTTP_GET, handleGetStreams);
  server.on("/api/streams", HTTP_POST, handlePostStreams);
  server.on("/api/player", HTTP_GET, handlePlayer);
  server.on("/api/player", HTTP_POST, handlePlayer);
  server.on("/api/mixer", HTTP_GET, handleMixer);
  server.on("/api/mixer", HTTP_POST, handleMixer);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/config/export", HTTP_GET, handleExportConfig);
  server.on("/api/config/import", HTTP_POST, handleImportConfig);
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi/save", HTTP_POST, handleWiFiSave);
  server.on("/api/wifi/status", HTTP_GET, handleWiFiStatus);
  server.on("/api/wifi/config", HTTP_GET, handleWiFiConfig);
  server.on("/api/proxy", HTTP_GET, handleProxyRequest);
  server.on("/api/proxy", HTTP_POST, handleProxyRequest);
  server.on("/api/proxy", HTTP_HEAD, handleProxyRequest);
  server.on("/api/proxy", HTTP_HEAD, handleProxyRequest);
  server.on("/w", HTTP_GET, handleSimpleWebPage);
  server.on("/w", HTTP_POST, handleSimpleWebPage);
  server.serveStatic("/", SPIFFS, "/player.html");
  server.serveStatic("/playlist", SPIFFS, "/playlist.html");
  server.serveStatic("/wifi", SPIFFS, "/wifi.html");
  server.serveStatic("/config", SPIFFS, "/config.html");
  server.serveStatic("/about", SPIFFS, "/about.html");
  server.serveStatic("/styles.css", SPIFFS, "/styles.css");
  server.serveStatic("/scripts.js", SPIFFS, "/scripts.js");
  server.serveStatic("/pico.min.css", SPIFFS, "/pico.min.css");
}


/**
 * @brief Connect to WiFi networks
 * Handles connection to configured WiFi networks with scanning and fallback to AP mode
 * @return true if connected to a network, false otherwise
 */
bool connectToWiFi() {
  static bool firstConnection = true;  // Track if this is the first connection attempt
  bool connected = false;
  if (wifiNetworkCount > 0) {
    WiFi.setHostname("NetTuner");
    // First, scan for available networks
    Serial.println("Scanning for available WiFi networks...");
    // Show appropriate status based on whether this is first connection or reconnection
    if (firstConnection) {
      display->showLogo();
    } else {
      display->showStatus("WiFi scanning", "", "");
    }
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d networks\n", n);
    // Create array to track which configured networks are available
    bool networkAvailable[MAX_WIFI_NETWORKS] = {false};
    // Check which configured networks are available
    for (int i = 0; i < wifiNetworkCount; i++) {
      if (strlen(ssid[i]) > 0) {
        for (int j = 0; j < n; j++) {
          if (strcmp(WiFi.SSID(j).c_str(), ssid[i]) == 0) {
            networkAvailable[i] = true;
            Serial.printf("Network %s is available\n", ssid[i]);
            break;
          }
        }
        if (!networkAvailable[i]) {
          Serial.printf("Network %s is not available\n", ssid[i]);
        }
      }
    }
    // Try to connect to available configured networks in background
    for (int i = 0; i < wifiNetworkCount; i++) {
      if (strlen(ssid[i]) > 0 && networkAvailable[i]) {
        display->turnOn();
        Serial.printf("Attempting to connect to %s...\n", ssid[i]);
        display->showStatus("WiFi connecting", String(ssid[i]), "");
        WiFi.begin(ssid[i], password[i]);
        int wifiAttempts = 0;
        const int maxAttempts = 15;
        while (WiFi.status() != WL_CONNECTED && wifiAttempts < maxAttempts) {
          delay(500);
          Serial.print(".");
          wifiAttempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("Connected to %s\n", ssid[i]);
          connected = true;
          // Update display with connection info
          Serial.print("IP Address: ");
          Serial.println(WiFi.localIP().toString());
          display->showStatus(String(WiFi.SSID()), "", WiFi.localIP().toString());
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
  // If not connected to any network, AP mode is already active
  if (connected) {
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP().toString());
    display->turnOn();
    display->showStatus("AP mode active", String(WiFi.SSID()), WiFi.localIP().toString());
    // Mark that first connection has been completed
    firstConnection = false;  
  } else {
    // If we reach here, no networks were connected
    if (wifiNetworkCount > 0) {
      Serial.println("Failed to connect to any configured WiFi network");
    } else {
      Serial.println("No WiFi networks configured");
    }
  }
  // Return connection status
  return connected;
}


/**
 * @brief Arduino main loop function
 * Handles web server requests, WebSocket events, rotary encoder input, and MPD commands
 * This is the main application loop that runs continuously after setup()
 */
void loop() {
  handleRotary();          // Process rotary encoder input
  if (touchPlay) touchPlay->handle();  // Process touch play button
  if (touchNext) touchNext->handle();  // Process touch next button
  if (touchPrev) touchPrev->handle();  // Process touch previous button
  handleTouch();           // Process touch button actions
  server.handleClient();   // Process incoming web requests
  webSocket.loop();        // Process WebSocket events
  mpdInterface.handleClient();       // Process MPD commands
  handleBoardButton();     // Process board button input
  
  // Periodically update display for scrolling text animation
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 500) {  // Update every 500ms for smooth scrolling
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Check audio connection status with improved error recovery
  static unsigned long streamStoppedTime = 0;
  if (player.getAudioObject()) {
    // Check if audio is still connected
    if (player.isPlaying()) {
      if (!player.isRunning()) {
        Serial.println("Audio stream stopped unexpectedly");
        // Attempt to restart the stream if it was playing
        if (strlen(player.getStreamUrl()) > 0) {
          // Wait 1 second before attempting to restart (non-blocking)
          if (streamStoppedTime == 0) {
            // First time detecting the stream has stopped
            streamStoppedTime = millis();
            Serial.println("Waiting 1 second before restart attempt...");
          } else if (millis() - streamStoppedTime >= 1000) {
            // 1 second has passed, attempt to restart
            Serial.println("Attempting to restart stream...");
            // Resume the current stream
            player.startStream();
            // Reset the timer
            streamStoppedTime = 0;
          }
        }
      } else {
        // Stream is running
        streamStoppedTime = 0;
        // Update the bitrate if it has changed
        player.updateBitrate();
      }

      // Send status to clients every 3 seconds instead of 2 to reduce load
      static unsigned long lastStatusUpdate = 0;
      if (millis() - lastStatusUpdate > 3000) {  // Changed from 2000 to 3000
        // Only send if there are connected clients
        if (webSocket.connectedClients() > 0) {
          // FIXME Send partial status update
          sendStatusToClients(true);
        }
        // Update the timestamp
        lastStatusUpdate = millis();
      }
    }
  }
  
  // Periodic cleanup with error recovery - add network status check
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 60000) {  // Every 60 seconds instead of 30
    lastCleanup = millis();
    // Check WiFi status and reconnect if needed
    if (wifiNetworkCount > 0 && WiFi.status() != WL_CONNECTED) {
      connectToWiFi();
    }
  }
  
  // Handle display timeout with configurable timeout value
  display->handleTimeout(player.isPlaying(), millis());

  // Small delay to prevent busy waiting and reduce network load
  delay(150);  // Increased from 100 to 150 to reduce CPU usage
}


/**
 * @brief Arduino setup function
 * Initializes all system components including WiFi, audio, display, and servers
 * This function is called once at startup to configure the hardware and software components.
 */
void setup() {
  Serial.begin(115200);
  // Print program name and build timestamp
  Serial.println("NetTuner - An ESP32-based internet radio player with MPD protocol support");
  Serial.print("Build timestamp: ");
  Serial.println(BUILD_TIME);
  
  // Initialize PSRAM if available
  #if defined(BOARD_HAS_PSRAM)
  if (psramInit()) {
    Serial.println("PSRAM initialized successfully");
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("PSRAM initialization failed");
  }
  #endif

  // Initialize SPIFFS with error recovery
  if (!initSPIFFS()) {
    Serial.println("ERROR: Failed to initialize SPIFFS");
    return;
  }
  // Load configuration
  loadConfig();
  
  // Validate display type
  if (config.display_type < 0 || config.display_type >= getDisplayTypeCount()) {
    config.display_type = 0; // Default to first display type
  }
  // Initialize LED pin if configured
  if (config.led_pin >= 0) {
    pinMode(config.led_pin, OUTPUT);
    digitalWrite(config.led_pin, LOW);  // Turn off LED initially
  }
  // Initialize board button with pull-up resistor if configured
  if (config.board_button >= 0) {
    pinMode(config.board_button, INPUT_PULLUP);
    // Attach interrupt handler for board button press
    attachInterrupt(digitalPinToInterrupt(config.board_button), boardButtonISR, FALLING);
  }
  // Initialize OLED display
  // Configure I2C pins
  Wire.begin(config.display_sda, config.display_scl);
  // Get display dimensions based on display type
  int displayWidth, displayHeight;
  if (!getDisplaySize(config.display_type, &displayWidth, &displayHeight)) {
    // Fallback to default if invalid display type
    displayWidth = 128;
    displayHeight = 64;
  }
  // Create display objects after config is loaded
  displayOLED = new Adafruit_SSD1306(displayWidth, displayHeight, &Wire, -1);
  display = new Display(*displayOLED, (enum display_t)config.display_type);
  display->begin();
  
  // Initialize touch buttons
  if (config.touch_play >= 0) {
    touchPlay = new TouchButton(config.touch_play, config.touch_threshold, config.touch_debounce);
  }
  if (config.touch_next >= 0) {
    touchNext = new TouchButton(config.touch_next, config.touch_threshold, config.touch_debounce);
  }
  if (config.touch_prev >= 0) {
    touchPrev = new TouchButton(config.touch_prev, config.touch_threshold, config.touch_debounce);
  }
  // Load WiFi credentials with error recovery
  loadWiFiCredentials();
  // Connect to WiFi with error handling
  connectToWiFi();
  // Always start AP mode as a control mechanism
  Serial.println("Starting Access Point mode...");
  display->showStatus("Starting AP Mode", "", "");
  
  // Start WiFi access point mode with error handling
  if (WiFi.softAP("NetTuner-Setup")) {
    Serial.println("Access Point Started");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP().toString());
    display->showStatus("AP Mode Active", "", WiFi.softAPIP().toString());
  } else {
    Serial.println("Failed to start Access Point");
    display->showStatus("AP Start Failed", "", "");
  }

  // Start mDNS responder
  #if defined(BOARD_HAS_PSRAM)
  if (MDNS.begin("NetTuner")) {
    Serial.println("MDNS responder started");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("mpd", "tcp", 6600);
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  #endif
  
  // Setup audio output with error handling
  Audio* audio = player.setupAudioOutput();
  // Setup rotary encoder with error handling
  setupRotaryEncoder();
  // Load playlist with error recovery
  player.loadPlaylist();
  // Validate loaded playlist
  player.getPlaylist()->validate();
    // Load player state
  player.loadPlayerState();
  if (player.isPlaying()) {
    // Update activity time
    display->setActivityTime(millis()); 
  }
  // Setup web server routes
  setupWebServer();
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
