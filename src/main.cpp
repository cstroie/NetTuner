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

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Web server
WebServer server(80);

// Audio components
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceHTTPStream *file = nullptr;
AudioOutputI2S *out = nullptr;
AudioFileSourceBuffer *buff = nullptr;

// Player state
String currentStream = "";
String currentStreamName = "";
bool isPlaying = false;
int volume = 50; // 0-100

// I2S configuration
#define I2S_DOUT      22
#define I2S_BCLK      25
#define I2S_LRC       26
#define I2S_SD        21

// OLED display
SSD1306Wire display(0x3c, 5, 4); // ADDRESS, SDA, SCL

// Rotary encoder
#define ROTARY_CLK 18
#define ROTARY_DT 19
#define ROTARY_SW 23

volatile int rotaryPosition = 0;
volatile int lastRotaryPosition = 0;
volatile bool rotaryCW = false;
volatile unsigned long lastRotaryTime = 0;
bool buttonPressed = false;

// Playlist
struct StreamInfo {
  String name;
  String url;
};

StreamInfo playlist[20];
int playlistCount = 0;
int currentSelection = 0;

// Task handles
TaskHandle_t audioTaskHandle = NULL;

// Function declarations
void audioTask(void *parameter);
void setupI2S();
void startStream(const String& url, const String& name);
void stopStream();
void loadPlaylist();
void savePlaylist();
void setupRotaryEncoder();
void IRAM_ATTR rotaryISR();
void updateDisplay();
void handleRotary();

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

void loop() {
  server.handleClient();
  handleRotary();
  delay(10);
}

void audioTask(void *parameter) {
  while (true) {
    if (mp3 && mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        isPlaying = false;
        updateDisplay();
      }
    }
    delay(1);
  }
}

void setupI2S() {
  // Configure I2S pins
  pinMode(I2S_SD, OUTPUT);
  digitalWrite(I2S_SD, HIGH); // Enable amplifier
}

void startStream(const String& url, const String& name) {
  stopStream();
  
  currentStream = url;
  currentStreamName = name;
  isPlaying = true;
  
  file = new AudioFileSourceHTTPStream(url.c_str());
  out = new AudioOutputI2S();
  mp3 = new AudioGeneratorMP3();
  
  // Set volume (0.0 to 1.0)
  out->SetGain(volume / 100.0);
  
  mp3->begin(file, out);
  
  updateDisplay();
}

void stopStream() {
  if (mp3) {
    if (mp3->isRunning()) {
      mp3->stop();
    }
    delete mp3;
    mp3 = nullptr;
  }
  
  if (out) {
    delete out;
    out = nullptr;
  }
  
  if (file) {
    delete file;
    file = nullptr;
  }
  
  isPlaying = false;
  currentStream = "";
  currentStreamName = "";
  
  updateDisplay();
}

void loadPlaylist() {
  playlistCount = 0;
  
  if (!SPIFFS.exists("/playlist.json")) {
    // Create default playlist
    File file = SPIFFS.open("/playlist.json", "w");
    if (file) {
      file.print("[]");
      file.close();
    }
    return;
  }
  
  File file = SPIFFS.open("/playlist.json", "r");
  if (!file) return;
  
  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size + 1]);
  file.readBytes(buf.get(), size);
  file.close();
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  JsonArray arr = doc.as<JsonArray>();
  playlistCount = 0;
  
  for (JsonVariant value : arr) {
    if (playlistCount >= 20) break;
    
    playlist[playlistCount].name = value["name"].as<String>();
    playlist[playlistCount].url = value["url"].as<String>();
    playlistCount++;
  }
}

void savePlaylist() {
  // Playlist saving is handled by the web API
}

void setupRotaryEncoder() {
  pinMode(ROTARY_CLK, INPUT);
  pinMode(ROTARY_DT, INPUT);
  pinMode(ROTARY_SW, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(ROTARY_CLK), rotaryISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_SW), []() {
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    
    if (interruptTime - lastInterruptTime > 200) {
      buttonPressed = true;
    }
    lastInterruptTime = interruptTime;
  }, FALLING);
}

void IRAM_ATTR rotaryISR() {
  static int lastCLK = 0;
  int CLK = digitalRead(ROTARY_CLK);
  int DT = digitalRead(ROTARY_DT);
  
  if (CLK != lastCLK && CLK == 1) {
    if (DT != CLK) {
      rotaryPosition++;
      rotaryCW = true;
    } else {
      rotaryPosition--;
      rotaryCW = false;
    }
  }
  lastCLK = CLK;
}

void handleRotary() {
  if (rotaryPosition != lastRotaryPosition) {
    int diff = rotaryPosition - lastRotaryPosition;
    
    if (diff > 0) {
      // Rotate clockwise - volume up or next item
      if (isPlaying) {
        volume = min(100, volume + 5);
        if (out) {
          out->SetGain(volume / 100.0);
        }
      } else {
        currentSelection = (currentSelection + 1) % max(1, playlistCount);
      }
    } else {
      // Rotate counter-clockwise - volume down or previous item
      if (isPlaying) {
        volume = max(0, volume - 5);
        if (out) {
          out->SetGain(volume / 100.0);
        }
      } else {
        currentSelection = (currentSelection - 1 + max(1, playlistCount)) % max(1, playlistCount);
      }
    }
    
    lastRotaryPosition = rotaryPosition;
    updateDisplay();
  }
  
  if (buttonPressed) {
    buttonPressed = false;
    
    if (playlistCount > 0 && currentSelection < playlistCount) {
      if (isPlaying) {
        stopStream();
      } else {
        startStream(playlist[currentSelection].url, playlist[currentSelection].name);
      }
    }
    updateDisplay();
  }
}

void updateDisplay() {
  display.clear();
  
  if (isPlaying) {
    display.drawString(0, 0, "PLAYING");
    display.drawString(0, 15, currentStreamName);
    display.drawString(0, 30, "Volume: " + String(volume) + "%");
  } else {
    display.drawString(0, 0, "STOPPED");
    if (playlistCount > 0 && currentSelection < playlistCount) {
      display.drawString(0, 15, playlist[currentSelection].name);
      display.drawString(0, 30, "Press to play");
    } else {
      display.drawString(0, 15, "No streams");
    }
    display.drawString(0, 45, "Vol: " + String(volume) + "%");
  }
  
  display.display();
}
