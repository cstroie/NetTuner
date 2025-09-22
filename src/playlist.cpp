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

#include "playlist.h"
#include "main.h"
#include <ArduinoJson.h>

extern bool readJsonFile(const char* filename, size_t maxFileSize, DynamicJsonDocument& doc);
extern bool writeJsonFile(const char* filename, DynamicJsonDocument& doc);

/**
 * @brief Playlist constructor
 */
Playlist::Playlist() {
  count = 0;
  current = 0;
  
  // Initialize playlist
  for (int i = 0; i < MAX_PLAYLIST_SIZE; i++) {
    playlist[i].name[0] = '\0';
    playlist[i].url[0] = '\0';
  }
}

/**
 * @brief Load playlist from SPIFFS storage
 * Reads playlist.json from SPIFFS and populates the playlist array
 * This function loads the playlist from SPIFFS with error recovery mechanisms.
 * If the playlist file is corrupted, it creates a backup and a new empty playlist.
 */
void Playlist::load() {
  count = 0;  // Reset playlist count
  // Load playlist using helper function
  DynamicJsonDocument doc(PLAYLIST_BUFFER_SIZE);
  if (!readJsonFile("/playlist.json", PLAYLIST_BUFFER_SIZE, doc)) {
    Serial.println("Failed to load playlist, continuing with empty playlist");
    return;
  }
  // Check if the JSON document is an array
  if (!doc.is<JsonArray>()) {
    Serial.println("Error: Playlist JSON is not an array");
    // Don't create an empty playlist, just return with empty playlist
    Serial.println("Continuing with empty playlist");
    return;
  }
  // Populate the playlist array
  JsonArray array = doc.as<JsonArray>();
  count = 0;
  // Iterate through the JSON array
  for (JsonObject item : array) {
    if (count >= MAX_PLAYLIST_SIZE) {
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
          // Add item to playlist
          strncpy(playlist[count].name, name, STREAM_NAME_SIZE - 1);
          playlist[count].name[STREAM_NAME_SIZE - 1] = '\0';
          strncpy(playlist[count].url, url, STREAM_URL_SIZE - 1);
          playlist[count].url[STREAM_URL_SIZE - 1] = '\0';
          count++;
        } else {
          Serial.println("Warning: Skipping stream with invalid URL format");
        }
      } else {
        Serial.println("Warning: Skipping stream with empty name or URL");
      }
    }
  }
  // Check if any valid streams were loaded
  if (count == 0) {
    Serial.println("Error: No valid streams found in playlist");
  } else {
    Serial.print("Loaded ");
    Serial.print(count);
    Serial.println(" streams from playlist");
  }
}

/**
 * @brief Save playlist to SPIFFS storage
 * Serializes the current playlist array to playlist.json
 * This function saves the current playlist to SPIFFS with backup functionality.
 * It creates a backup before saving and restores from backup if saving fails.
 */
void Playlist::save() {
  // Create JSON array
  DynamicJsonDocument doc(PLAYLIST_BUFFER_SIZE);
  JsonArray array = doc.to<JsonArray>();
  // Add playlist entries
  for (int i = 0; i < count; i++) {
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
    Serial.println("Saved playlist to SPIFFS");
  } else {
    Serial.println("Failed to save playlist to SPIFFS");
  }
}

/**
 * @brief Set playlist item at specific index
 * @param index Playlist index
 * @param name Stream name
 * @param url Stream URL
 */
void Playlist::setItem(int index, const char* name, const char* url) {
  if (index >= 0 && index < MAX_PLAYLIST_SIZE && name && url) {
    strncpy(playlist[index].name, name, STREAM_NAME_SIZE - 1);
    playlist[index].name[STREAM_NAME_SIZE - 1] = '\0';
    strncpy(playlist[index].url, url, STREAM_URL_SIZE - 1);
    playlist[index].url[STREAM_URL_SIZE - 1] = '\0';
    if (index >= count) {
      count = index + 1;
    }
  }
}

/**
 * @brief Add playlist item
 * @param name Stream name
 * @param url Stream URL
 */
void Playlist::addItem(const char* name, const char* url) {
  if (count < MAX_PLAYLIST_SIZE && name && url) {
    strncpy(playlist[count].name, name, STREAM_NAME_SIZE - 1);
    playlist[count].name[STREAM_NAME_SIZE - 1] = '\0';
    strncpy(playlist[count].url, url, STREAM_URL_SIZE - 1);
    playlist[count].url[STREAM_URL_SIZE - 1] = '\0';
    count++;
  }
}

/**
 * @brief Remove playlist item at specific index
 * @param index Playlist index to remove
 */
void Playlist::removeItem(int index) {
  if (index >= 0 && index < count) {
    // Shift all items after the removed item
    for (int i = index; i < count - 1; i++) {
      strncpy(playlist[i].name, playlist[i + 1].name, STREAM_NAME_SIZE - 1);
      playlist[i].name[STREAM_NAME_SIZE - 1] = '\0';
      strncpy(playlist[i].url, playlist[i + 1].url, STREAM_URL_SIZE - 1);
      playlist[i].url[STREAM_URL_SIZE - 1] = '\0';
    }
    // Clear the last item
    playlist[count - 1].name[0] = '\0';
    playlist[count - 1].url[0] = '\0';
    count--;
    
    // Adjust current if necessary
    if (current >= count) {
      current = count - 1;
    }
    if (current < 0) {
      current = 0;
    }
  }
}

/**
 * @brief Clear all playlist items
 */
void Playlist::clear() {
  for (int i = 0; i < count; i++) {
    playlist[i].name[0] = '\0';
    playlist[i].url[0] = '\0';
  }
  count = 0;
  current = 0;
}

/**
 * @brief Validate playlist integrity
 * Ensures playlist count and selection are within valid ranges
 */
void Playlist::validate() {
  // Validate playlist count
  if (count < 0 || count > MAX_PLAYLIST_SIZE) {
    Serial.println("Warning: Invalid playlist count detected, resetting to 0");
    count = 0;
  }
  
  // Validate current selection
  if (current < 0 || current >= count) {
    current = 0;
  }
}
