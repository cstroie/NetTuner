/*
 * NetTuner - An ESP32-based internet radio player
 * Copyright (C) 2025 Your Name
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
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

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
AudioOutputI2S *out = nullptr;              ///< I2S audio output

/**
 * @brief Player state variables
 * Track current playback status, stream information, and volume level
 */
String currentStream = "";      ///< URL of currently playing stream
String currentStreamName = "";  ///< Name of currently playing stream
bool isPlaying = false;         ///< Playback status flag
int volume = 50;                ///< Volume level (0-100)

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
volatile int rotaryPosition = 0;          ///< Current rotary encoder position
volatile int lastRotaryPosition = 0;      ///< Last recorded rotary encoder position
volatile bool rotaryCW = false;           ///< Rotation direction flag (true = clockwise)
bool buttonPressed = false;               ///< Rotary encoder button press flag

/**
 * @brief Playlist data structure
 * Stores information about available audio streams
 */
struct StreamInfo {
  String name;  ///< Human-readable name of the stream
  String url;   ///< URL of the audio stream
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
void startStream(const String& url, const String& name);
void stopStream();
void loadPlaylist();
void savePlaylist();
void setupRotaryEncoder();
void rotaryISR();
void updateDisplay();
void handleRotary();

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
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
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
  server.on("/", HTTP_GET, []() {
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  
  server.on("/playlist.html", HTTP_GET, []() {
    File file = SPIFFS.open("/playlist.html", "r");
    if (!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  
  server.on("/api/streams", HTTP_GET, []() {
    File file = SPIFFS.open("/playlist.json", "r");
    if (!file) {
      server.send(200, "application/json", "[]");
      return;
    }
    server.streamFile(file, "application/json");
    file.close();
  });
  
  server.on("/api/streams", HTTP_POST, []() {
    String json = server.arg("plain");
    File file = SPIFFS.open("/playlist.json", "w");
    if (!file) {
      server.send(500, "text/plain", "Failed to save playlist");
      return;
    }
    file.print(json);
    file.close();
    loadPlaylist(); // Reload playlist
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/api/play", HTTP_POST, []() {
    String url = server.arg("url");
    String name = server.arg("name");
    startStream(url, name);
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/api/stop", HTTP_POST, []() {
    stopStream();
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/api/volume", HTTP_POST, []() {
    String vol = server.arg("volume");
    volume = vol.toInt();
    if (out) {
      out->SetGain(volume / 100.0);
    }
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/api/status", HTTP_GET, []() {
    String status = "{";
    status += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
    status += "\"currentStream\":\"" + currentStream + "\",";
    status += "\"currentStreamName\":\"" + currentStreamName + "\",";
    status += "\"volume\":" + String(volume);
    status += "}";
    server.send(200, "application/json", status);
  });
  
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  
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
 * @brief Audio processing task function
 * Runs on core 0 to handle continuous audio decoding and playback
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
void startStream(const String& url, const String& name) {
  stopStream();  // Stop any currently playing stream
  
  currentStream = url;      // Store the stream URL
  currentStreamName = name; // Store the stream name
  isPlaying = true;         // Set playback status to playing
  
  // Create new audio components for the stream
  file = new AudioFileSourceHTTPStream(url.c_str());  // HTTP stream source
  out = new AudioOutputI2S();                         // I2S output
  mp3 = new AudioGeneratorMP3();                      // MP3 decoder
  
  // Set volume (0.0 to 1.0)
  out->SetGain(volume / 100.0);
  
  // Start the decoding process
  mp3->begin(file, out);
  
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
  currentStream = "";       // Clear current stream URL
  currentStreamName = "";   // Clear current stream name
  
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
    }
    return;
  }
  
  // Open the playlist file for reading
  File file = SPIFFS.open("/playlist.json", "r");
  if (!file) return;  // Return if file couldn't be opened
  
  // Read the entire file into a buffer
  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size + 1]);
  file.readBytes(buf.get(), size);
  file.close();
  
  // Parse the JSON data
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Extract the array of streams
  JsonArray arr = doc.as<JsonArray>();
  playlistCount = 0;
  
  // Populate the playlist array with stream information
  for (JsonVariant value : arr) {
    if (playlistCount >= 20) break;  // Limit to 20 entries
    
    playlist[playlistCount].name = value["name"].as<String>();
    playlist[playlistCount].url = value["url"].as<String>();
    playlistCount++;
  }
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
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    
    // Debounce the button press (ignore if less than 200ms since last press)
    if (interruptTime - lastInterruptTime > 200) {
      buttonPressed = true;
    }
    lastInterruptTime = interruptTime;
  }, FALLING);
}

/**
 * @brief Rotary encoder interrupt service routine
 * Called when the rotary encoder position changes
 * This function runs in interrupt context and must be in IRAM
 */
void IRAM_ATTR rotaryISR() {
  static int lastCLK = 0;
  int CLK = digitalRead(ROTARY_CLK);  // Read clock signal
  int DT = digitalRead(ROTARY_DT);    // Read data signal
  
  // Only process when CLK transitions to high
  if (CLK != lastCLK && CLK == 1) {
    // Determine rotation direction based on CLK and DT relationship
    if (DT != CLK) {
      rotaryPosition++;      // Clockwise rotation
      rotaryCW = true;
    } else {
      rotaryPosition--;      // Counter-clockwise rotation
      rotaryCW = false;
    }
  }
  lastCLK = CLK;
}

/**
 * @brief Handle rotary encoder input
 * Processes rotation and button press events from the rotary encoder
 */
void handleRotary() {
  // Check if rotary encoder position has changed
  if (rotaryPosition != lastRotaryPosition) {
    int diff = rotaryPosition - lastRotaryPosition;
    
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
    } else {
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
    
    lastRotaryPosition = rotaryPosition;  // Update last position
    updateDisplay();                      // Refresh display with new values
  }
  
  // Process button press if detected
  if (buttonPressed) {
    buttonPressed = false;  // Reset button press flag
    
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
