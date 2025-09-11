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

#include "player.h"
#include "playlist.h"
#include "main.h"
#include <ArduinoJson.h>


/**
 * @brief Player constructor
 */
Player::Player() {
  playerState.playing = false;
  playerState.volume = 8;
  playerState.bass = 0;
  playerState.mid = 0;
  playerState.treble = 0;
  playerState.playlistIndex = 0;
  playerState.lastSaveTime = 0;
  playerState.dirty = false;
  playerState.playStartTime = 0;
  playerState.totalPlayTime = 0;
  audio = nullptr;
  playlist = new Playlist();
  
  // Initialize stream info
  streamInfo.url[0] = '\0';
  streamInfo.name[0] = '\0';
  streamInfo.title[0] = '\0';
  streamInfo.icyUrl[0] = '\0';
  streamInfo.iconUrl[0] = '\0';
  streamInfo.bitrate = 0;
}

/**
 * @brief Set player volume
 * @param volume New volume level (0-22)
 */
void Player::setVolume(int volume) {
  playerState.volume = volume;
  if (audio) {
    audio->setVolume(volume);
  }
}

/**
 * @brief Set tone controls (bass, mid, treble)
 * Applies the tone settings to the audio output
 */
void Player::setTone() {
  // Apply tone settings to audio output
  if (audio) {
    audio->setTone(playerState.bass, playerState.mid, playerState.treble);
  }
}

/**
 * @brief Set tone controls (bass, mid, treble)
 * Applies the tone settings to the audio output
 * @param bass Bass level (-6 to 6)
 * @param mid Mid level (-6 to 6)
 * @param treble Treble level (-6 to 6)
 */
void Player::setTone(int bass, int mid, int treble) {
  // Validate and set tone values
  playerState.bass = constrain(bass, -6, 6);
  playerState.mid = constrain(mid, -6, 6);
  playerState.treble = constrain(treble, -6, 6);
  
  // Apply tone settings to audio output
  setTone();
}

/**
 * @brief Set stream URL
 * @param url New stream URL
 */
void Player::setStreamUrl(const char* url) {
  if (url) {
    strncpy(streamInfo.url, url, sizeof(streamInfo.url) - 1);
    streamInfo.url[sizeof(streamInfo.url) - 1] = '\0';
  }
}

/**
 * @brief Set stream name
 * @param name New stream name
 */
void Player::setStreamName(const char* name) {
  if (name) {
    strncpy(streamInfo.name, name, sizeof(streamInfo.name) - 1);
    streamInfo.name[sizeof(streamInfo.name) - 1] = '\0';
  }
}

/**
 * @brief Set stream title
 * @param title New stream title
 */
void Player::setStreamTitle(const char* title) {
  if (title) {
    strncpy(streamInfo.title, title, sizeof(streamInfo.title) - 1);
    streamInfo.title[sizeof(streamInfo.title) - 1] = '\0';
  }
}

/**
 * @brief Set stream ICY URL
 * @param icyUrl New stream ICY URL
 */
void Player::setStreamIcyUrl(const char* icyUrl) {
  if (icyUrl) {
    strncpy(streamInfo.icyUrl, icyUrl, sizeof(streamInfo.icyUrl) - 1);
    streamInfo.icyUrl[sizeof(streamInfo.icyUrl) - 1] = '\0';
  }
}

/**
 * @brief Set stream icon URL
 * @param iconUrl New stream icon URL
 */
void Player::setStreamIconUrl(const char* iconUrl) {
  if (iconUrl) {
    strncpy(streamInfo.iconUrl, iconUrl, sizeof(streamInfo.iconUrl) - 1);
    streamInfo.iconUrl[sizeof(streamInfo.iconUrl) - 1] = '\0';
  }
}

/**
 * @brief Clear all stream information
 */
void Player::clearStreamInfo() {
  streamInfo.url[0] = '\0';
  streamInfo.name[0] = '\0';
  streamInfo.title[0] = '\0';
  streamInfo.icyUrl[0] = '\0';
  streamInfo.iconUrl[0] = '\0';
  streamInfo.bitrate = 0;
}

/**
 * @brief Load player state from SPIFFS
 */
void Player::loadPlayerState() {
  DynamicJsonDocument doc(512);
  if (readJsonFile("/player.json", 512, doc)) {
    playerState.playing = doc["playing"] | false;
    playerState.volume = doc["volume"] | 8;
    playerState.bass = doc["bass"] | 0;
    playerState.mid = doc["mid"] | 0;
    playerState.treble = doc["treble"] | 0;
    playerState.playlistIndex = doc["playlistIndex"] | 0;
    Serial.println("Loaded player state from SPIFFS");
  } else {
    Serial.println("No player state file found, using defaults");
    playerState.playing = false;
    playerState.volume = 8;
    playerState.bass = 0;
    playerState.mid = 0;
    playerState.treble = 0;
    playerState.playlistIndex = 0;
  }
  // Apply loaded state
  if (audio) {
    audio->setVolume(playerState.volume);
    audio->setTone(playerState.bass, playerState.mid, playerState.treble);
  }
  if (playerState.playlistIndex >= 0 && playerState.playlistIndex < playlist->getPlaylistCount()) {
    // currentSelection is now handled within Player class
  }
  // If was playing, resume playback
  if (playerState.playing && playlist->getPlaylistCount() > 0 && playerState.playlistIndex < playlist->getPlaylistCount()) {
    Serial.println("Resuming playback from saved state");
    startStream(playlist->getPlaylistItem(playerState.playlistIndex).url, playlist->getPlaylistItem(playerState.playlistIndex).name);
  }
}

/**
 * @brief Save player state to SPIFFS
 */
void Player::savePlayerState() {
  DynamicJsonDocument doc(512);
  doc["playing"] = playerState.playing;
  doc["volume"] = playerState.volume;
  doc["bass"] = playerState.bass;
  doc["mid"] = playerState.mid;
  doc["treble"] = playerState.treble;
  doc["playlistIndex"] = playerState.playlistIndex;
  if (writeJsonFile("/player.json", doc)) {
    Serial.println("Saved player state to SPIFFS");
    playerState.dirty = false;
  } else {
    Serial.println("Failed to save player state to SPIFFS");
  }
}

void Player::loadPlaylist() {
  playlist->loadPlaylist();
}

void Player::savePlaylist() {
  playlist->savePlaylist();
}

void Player::setPlaylistItem(int index, const char* name, const char* url) {
  playlist->setPlaylistItem(index, name, url);
}

void Player::addPlaylistItem(const char* name, const char* url) {
  playlist->addPlaylistItem(name, url);
}

void Player::removePlaylistItem(int index) {
  playlist->removePlaylistItem(index);
}

void Player::clearPlaylist() {
  playlist->clearPlaylist();
}

int Player::getPlaylistCount() const { 
  return playlist->getPlaylistCount(); 
}

const StreamInfo& Player::getPlaylistItem(int index) const { 
  return playlist->getPlaylistItem(index); 
}

/**
 * @brief Mark player state as dirty (needs saving)
 */
void Player::markPlayerStateDirty() {
  playerState.dirty = true;
}

/**
 * @brief Start streaming an audio stream
 * Stops any currently playing stream and begins playing a new one
 * If called without parameters, resumes playback of streamURL if available
 * @param url URL of the audio stream to play (optional)
 * @param name Human-readable name of the stream (optional)
 */
void Player::startStream(const char* url, const char* name) {
  bool resume = false;
  // Stop the currently playing stream if the stream changes
  if (audio && url && strlen(url) > 0) {
    // Stop first
    stopStream();
  }
  // If no URL provided, check if we have a current stream to resume
  if (!url || strlen(url) == 0) {
    if (strlen(streamInfo.url) > 0) {
      // Resume playback of current stream
      url = streamInfo.url;
      // Use current name if available, otherwise use a default
      if (!name || strlen(name) == 0) {
        name = (strlen(streamInfo.name) > 0) ? String(streamInfo.name).c_str() : "Unknown Station";
      }
      // We are resuming playback
      resume = true;
    } else {
      Serial.println("Error: No URL provided and no current stream to resume");
      return;
    }
  }
  // Validate inputs
  if (!url || !name) {
    Serial.println("Error: NULL stream URL or name pointer passed to startStream");
    return;
  }
  // Check for empty strings
  if (strlen(url) == 0 || strlen(name) == 0) {
    Serial.println("Error: Empty stream URL or name passed to startStream");
    return;
  }
  // Validate URL format
  if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
    Serial.println("Error: Invalid URL format");
    return;
  }
  // Keep the stream url and name if they are new
  if (!resume) {
    strncpy(streamInfo.url, url, sizeof(streamInfo.url) - 1);
    streamInfo.url[sizeof(streamInfo.url) - 1] = '\0';
    strncpy(streamInfo.name, name, sizeof(streamInfo.name) - 1);
    streamInfo.name[sizeof(streamInfo.name) - 1] = '\0';
  }
  // Set playback status to playing
  playerState.playing = true;
  // Track play time
  playerState.playStartTime = millis() / 1000;  // Store in seconds
  // Turn on LED when playing
  digitalWrite(config.led_pin, HIGH);
  // Use ESP32-audioI2S to play the stream
  if (audio) {
    bool audioConnected = audio->connecttohost(url);
    if (!audioConnected) {
      Serial.println("Error: Failed to connect to audio stream");
      playerState.playing = false;
      streamInfo.bitrate = 0;
    } else {
      playerState.playing = true;
      Serial.println("Successfully connected to audio stream");
    }
  }
  updateDisplay();        // Refresh the display with new playback info
  sendStatusToClients();  // Notify clients of status change
}

/**
 * @brief Stop the currently playing stream
 * Cleans up audio components and resets playback state
 * This function stops audio playback, clears stream information, and resets
 * the playback state to stopped.
 */
void Player::stopStream() {
  // Stop the audio playback
  if (audio) {
    audio->stopSong();
  }
  playerState.playing = false;             // Set playback status to stopped
  clearStreamInfo();
  // Update total play time when stopping
  if (playerState.playStartTime > 0) {
    playerState.totalPlayTime += (millis() / 1000) - playerState.playStartTime;
    playerState.playStartTime = 0;
  }
  // Turn off LED when stopped
  digitalWrite(config.led_pin, LOW);
  updateDisplay();  // Refresh the display
  sendStatusToClients();  // Notify clients of status change
}

/**
 * @brief Initialize audio output interface
 * Configures the selected audio output method
 * This function initializes the ESP32-audioI2S library with I2S pin configuration
 * and sets up the audio buffer with an increased size for better performance.
 * @return Pointer to the initialized Audio object
 */
Audio* Player::setupAudioOutput() {
  // Initialize ESP32-audioI2S
  audio = new Audio(false); // false = use I2S, true = use DAC
  audio->setPinout(config.i2s_bclk, config.i2s_lrc, config.i2s_dout);
  audio->setVolume(playerState.volume); // Use 0-22 scale directly
  audio->setBufsize(65536, 0); // Increased buffer size to 64KB for better streaming performance
  return audio;
}

void Player::handleAudio() {
  if (audio) {
    audio->loop();
  }
}
