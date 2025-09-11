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

#ifndef PLAYER_H
#define PLAYER_H

#include <Arduino.h>

// Forward declarations
class Audio;

// Stream information variables
struct StreamInfoData {
  char url[256];
  char name[128];
  char title[128];
  char icyUrl[256];
  char iconUrl[256];
  int bitrate;
};

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

// Player class declaration
class Player {
private:
  PlayerState playerState;
  StreamInfoData streamInfo;
  
public:
  // Constructor
  Player();
  
  // Player state methods
  void loadPlayerState();
  void savePlayerState();
  void markPlayerStateDirty();
  
  // Getters
  bool isPlaying() const { return playerState.playing; }
  int getVolume() const { return playerState.volume; }
  int getBass() const { return playerState.bass; }
  int getMid() const { return playerState.mid; }
  int getTreble() const { return playerState.treble; }
  int getPlaylistIndex() const { return playerState.playlistIndex; }
  bool isDirty() const { return playerState.dirty; }
  int getBitrate() const { return streamInfo.bitrate; }
  
  // Setters
  void setPlaying(bool playing) { playerState.playing = playing; }
  void setVolume(int volume);
  void setBass(int bass) { playerState.bass = bass; }
  void setMid(int mid) { playerState.mid = mid; }
  void setTreble(int treble) { playerState.treble = treble; }
  void setPlaylistIndex(int index) { playerState.playlistIndex = index; }
  void setBitrate(int newBitrate) { streamInfo.bitrate = newBitrate; }
  
  // Stream info getters
  const char* getStreamUrl() const { return streamInfo.url; }
  const char* getStreamName() const { return streamInfo.name; }
  const char* getStreamTitle() const { return streamInfo.title; }
  const char* getStreamIcyUrl() const { return streamInfo.icyUrl; }
  const char* getStreamIconUrl() const { return streamInfo.iconUrl; }
  
  // Stream info setters
  void setStreamUrl(const char* url);
  void setStreamName(const char* name);
  void setStreamTitle(const char* title);
  void setStreamIcyUrl(const char* icyUrl);
  void setStreamIconUrl(const char* iconUrl);
  void clearStreamInfo();
  
  
  // Audio control methods
  void startStream(const char* url = nullptr, const char* name = nullptr);
  void stopStream();
};

#endif // PLAYER_H
