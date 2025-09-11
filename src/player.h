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
class Playlist;

// Stream information variables
/**
 * @brief Stream information structure
 * Contains all the information about the current stream
 */
struct StreamInfoData {
  char url[256];    ///< Stream URL
  char name[128];   ///< Stream name
  char title[128];  ///< Current track title
  char icyUrl[256]; ///< ICY URL
  char iconUrl[256];///< Stream icon URL
  int bitrate;      ///< Stream bitrate
};

/**
 * @brief Player state structure
 * Contains all the state information for the player
 */
struct PlayerState {
  bool playing;                ///< Current playback status (true = playing, false = stopped)
  int volume;                  ///< Current volume level (0-22, ESP32-audioI2S scale)
  int bass;                    ///< Bass tone control (-6 to 6)
  int mid;                     ///< Mid tone control (-6 to 6)
  int treble;                  ///< Treble tone control (-6 to 6)
  int playlistIndex;           ///< Current selected playlist index
  unsigned long lastSaveTime;  ///< Timestamp of last state save
  bool dirty;                  ///< Flag indicating if state has changed and needs saving
  unsigned long playStartTime; ///< Timestamp when current playback started
  unsigned long totalPlayTime; ///< Total playback time in seconds
};

// Player class declaration
class Player {
private:
  PlayerState playerState;
  StreamInfoData streamInfo;
  Playlist* playlist;
  Audio* audio;
  
public:
  // Constructor
  Player();
  
  // Audio getter
  Audio* getAudio() const { return audio; }
  
  // Player state methods
  void clearPlayerState();
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
  int getPlaylistCount() const;
  bool isDirty() const { return playerState.dirty; }
  int getBitrate() const { return streamInfo.bitrate; }
  unsigned long getPlayStartTime() const { return playerState.playStartTime; }
  unsigned long getTotalPlayTime() const { return playerState.totalPlayTime; }
  
  // Setters
  void setPlaying(bool playing) { playerState.playing = playing; }
  void setVolume(int volume);
  void setTone();
  void setTone(int bass, int mid, int treble);
  void setBass(int bass) { playerState.bass = bass; }
  void setMid(int mid) { playerState.mid = mid; }
  void setTreble(int treble) { playerState.treble = treble; }
  void setPlaylistIndex(int index) { playerState.playlistIndex = index; }
  void setBitrate(int newBitrate) { streamInfo.bitrate = newBitrate; }
  void setPlayStartTime(unsigned long time) { playerState.playStartTime = time; }
  void setTotalPlayTime(unsigned long time) { playerState.totalPlayTime = time; }
  void addPlayTime(unsigned long time) { playerState.totalPlayTime += time; }

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
  
  // Playlist getters
  const struct StreamInfo& getPlaylistItem(int index) const;
  Playlist* getPlaylist() const { return playlist; }

  // Playlist methods
  void loadPlaylist();
  void savePlaylist();
  void setPlaylistItem(int index, const char* name, const char* url);
  void addPlaylistItem(const char* name, const char* url);
  void removePlaylistItem(int index);
  void clearPlaylist();
  
  // Audio control methods
  void startStream(const char* url = nullptr, const char* name = nullptr);
  void stopStream();
  
  // Audio setup method
  Audio* setupAudioOutput();
  // Audio handler
  void handleAudio();
};

#endif // PLAYER_H
