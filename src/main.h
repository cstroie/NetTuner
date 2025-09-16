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
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "Audio.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <ESPmDNS.h>
#include "rotary.h"


// Forward declarations
class Audio;
class AsyncWebServer;
class AsyncWebSocket;
class WiFiServer;
class Adafruit_SSD1306;
class MPDInterface;
class WiFiClient;
class Display;
class RotaryEncoder;

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
  int display_type;    ///< OLED display type (index)
  int display_address; ///< OLED display I2C address
  int display_timeout; ///< Display timeout in seconds
  int touch_play;      ///< Touch button play/pause pin
  int touch_next;      ///< Touch button next/volume-up pin
  int touch_prev;      ///< Touch button previous/volume-down pin
  int touch_threshold; ///< Touch threshold value
  int touch_debounce;  ///< Touch debounce time in milliseconds
};
extern Config config;

// Constants
#define MAX_WIFI_NETWORKS 5
#define MAX_PLAYLIST_SIZE 20
#define VALIDATE_URL(url) (url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0))

// Global variables
extern const char* BUILD_TIME;
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern WiFiServer mpdServer;
extern Display* display;
extern TaskHandle_t audioTaskHandle;
extern char ssid[MAX_WIFI_NETWORKS][64];
extern char password[MAX_WIFI_NETWORKS][64];
extern int wifiNetworkCount;

// Forward declarations for global functions
void updateDisplay();
void sendStatusToClients(bool fullStatus);
void sendStatusToClients();
void handleRotary();
void handleTouch();
void audioTask(void *pvParameters);
void loadConfig();
void saveConfig();
void loadWiFiCredentials();
void saveWiFiCredentials();

// Web server handlers
void handleSimpleWebPage(AsyncWebServerRequest *request);
void handleGetStreams(AsyncWebServerRequest *request);
void handlePostStreams(AsyncWebServerRequest *request);
void handleGetConfig(AsyncWebServerRequest *request);
void handlePostConfig(AsyncWebServerRequest *request);
void handleExportConfig(AsyncWebServerRequest *request);
void handleImportConfig(AsyncWebServerRequest *request);
void handleWiFiScan(AsyncWebServerRequest *request);
void handleWiFiSave(AsyncWebServerRequest *request);
void handleWiFiStatus(AsyncWebServerRequest *request);
void handleWiFiConfig(AsyncWebServerRequest *request);
void handleProxyRequest(AsyncWebServerRequest *request);

// WebSocket handlers
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);

// Audio callbacks
void audio_showstreamtitle(const char *info);
void audio_showstation(const char *info);
void audio_bitrate(const char *info);
void audio_info(const char *info);
void audio_icyurl(const char *info);
void audio_icydescription(const char *info);
void audio_id3data(const char *info);

// Utility functions
String generateStatusJSON(bool fullStatus);

// JSON file helper functions
bool readJsonFile(const char* filename, size_t maxFileSize, DynamicJsonDocument& doc);
bool writeJsonFile(const char* filename, DynamicJsonDocument& doc);

#endif // MAIN_H
