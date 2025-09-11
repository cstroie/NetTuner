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

// Player class declaration
class Player {
private:
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
  
  PlayerState playerState;
  
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
  
  // Setters
  void setPlaying(bool playing) { playerState.playing = playing; }
  void setVolume(int volume);
  void setBass(int bass) { playerState.bass = bass; }
  void setMid(int mid) { playerState.mid = mid; }
  void setTreble(int treble) { playerState.treble = treble; }
  void setPlaylistIndex(int index) { playerState.playlistIndex = index; }
  
  // Audio control methods
  void startStream(const char* url = nullptr, const char* name = nullptr);
  void stopStream();
};

#endif // PLAYER_H
