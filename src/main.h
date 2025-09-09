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

#ifndef MAIN_H
#define MAIN_H

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
#include <ESPmDNS.h>


// Forward declarations
class Audio;
class WebServer;
class WebSocketsServer;
class WiFiServer;
class Adafruit_SSD1306;
class MPDInterface;
class WiFiClient;
class Display;  // Forward declaration for Display class

// Structure declarations
struct StreamInfo {
  char name[128];
  char url[256];
};

// Configuration structure
struct Config {
  int i2s_dout;        ///< I2S Data Out pin
  int i2s_bclk;        ///< I2S Bit Clock pin
  int i2s_lrc;         ///< I2S Left/Right Clock pin
  int led_pin;         ///< LED indicator pin
  int rotary_clk;      ///< Rotary encoder clock pin
  int rotary_dt;       ///< Rotary encoder data pin
  int rotary_sw;       ///< Rotary encoder switch pin
  int board_button;    ///< Board button pin
  int display_sda;     ///< OLED display SDA pin
  int display_scl;     ///< OLED display SCL pin
  int display_width;   ///< OLED display width
  int display_height;  ///< OLED display height
  int display_address; ///< OLED display I2C address
};
extern Config config;

// Constants
#define MAX_WIFI_NETWORKS 5
#define MAX_PLAYLIST_SIZE 20
#define VALIDATE_URL(url) (url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0))

// Global variables
extern int bitrate;
extern volatile bool isPlaying;
extern unsigned long lastActivityTime;
extern bool displayOn;
extern unsigned long startTime;
extern unsigned long playStartTime;
extern unsigned long totalPlayTime;
extern const char* BUILD_TIME;
extern const unsigned long BUILD_TIME_UNIX;
extern Audio* audio;
extern bool audioConnected;
extern WebServer server;
extern WebSocketsServer webSocket;
extern WiFiServer mpdServer;
extern WiFiClient mpdClient;
extern Display display;
extern StreamInfo playlist[MAX_PLAYLIST_SIZE];
extern int playlistCount;
extern int currentSelection;
extern TaskHandle_t audioTaskHandle;
extern char ssid[MAX_WIFI_NETWORKS][64];
extern char password[MAX_WIFI_NETWORKS][64];
extern int wifiNetworkCount;

// Stream information variables
struct StreamInfoData {
  char url[256];
  char name[128];
  char title[128];
  char icyUrl[256];
  char iconUrl[256];
};
extern StreamInfoData streamInfo;

// Player state tracking
struct PlayerState {
  bool playing;
  int volume;
  int bass;
  int mid;
  int treble;
  int playlistIndex;
  unsigned long lastSaveTime;
  bool dirty;
};
extern PlayerState playerState;

// Forward declarations for global functions
void stopStream();
void startStream(const char* url = nullptr, const char* name = nullptr);
void updateDisplay();
void sendStatusToClients();
void setupAudioOutput();
void loadPlaylist();
void savePlaylist();
void handleRotary();
void handleDisplayTimeout();
void audioTask(void *pvParameters);
void loadConfig();
void saveConfig();
void loadWiFiCredentials();
void saveWiFiCredentials();

// Web server handlers
void handleSimpleWebPage();
void handleGetStreams();
void handlePostStreams();
void handleGetConfig();
void handlePostConfig();
void handleExportConfig();
void handleImportConfig();
void handleWiFiScan();
void handleWiFiSave();
void handleWiFiStatus();
void handleWiFiConfig();

// WebSocket handlers
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

// Audio callbacks
void audio_showstreamtitle(const char *info);
void audio_showstation(const char *info);
void audio_bitrate(const char *info);
void audio_info(const char *info);
void audio_icyurl(const char *info);
void audio_icydescription(const char *info);
void audio_id3data(const char *info);

// Helper functions
bool initializeSPIFFS();

// Utility functions
String generateStatusJSON();


// Timestamp
#define BUILD_TIME_UNIX 1757065455

#endif // MAIN_H
