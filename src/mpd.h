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

#ifndef MPD_H
#define MPD_H

#include <Arduino.h>
#include <WiFi.h>
#include <Audio.h>

// Forward declarations
struct StreamInfo;

// External global variables
extern unsigned long startTime;
extern unsigned long totalPlayTime;
extern unsigned long playStartTime;

// External function declarations
extern void markPlayerStateDirty();
extern void savePlayerState();

/**
 * @brief MPD Interface Class
 * Encapsulates all MPD protocol functionality
 */
class MPDInterface {
private:
  WiFiServer& mpdServer;
  WiFiClient mpdClient;

  // References to global variables
  char* streamTitleRef;
  char* streamNameRef;
  char* streamURLRef;
  volatile bool& isPlayingRef;
  int& volumeRef;
  int& bitrateRef;
  int& playlistCountRef;
  int& currentSelectionRef;
  StreamInfo* playlistRef;
  Audio*& audioRef;

  // MPD command list state variables
  bool inCommandList = false;        ///< Flag indicating if we're in command list mode
  bool commandListOK = false;        ///< Flag indicating if we should send list_OK responses
  String commandList[50];            ///< Buffer for command list (max 50 commands)
  int commandListCount = 0;          ///< Number of commands in the current command list

  // MPD idle state variables
  bool inIdleMode = false;           ///< Flag indicating if we're in idle mode
  unsigned long lastTitleHash = 0;   ///< Hash of last stream title for change detection
  unsigned long lastStatusHash = 0;  ///< Hash of last status for change detection

public:
  MPDInterface(WiFiServer& server, char* streamTitle, char* streamName, char* streamURL,
               volatile bool& isPlaying, int& volume, int& bitrate, int& playlistCount,
               int& currentSelection, StreamInfo* playlist, Audio*& audio);

  /**
   * @brief Handle MPD client connections and process commands
   * Manages the MPD protocol connection state and command processing
   */
  void handleClient();

private:
  /**
   * @brief Handle idle mode monitoring and notifications
   * Monitors for changes in playback status and stream information
   */
  void handleIdleMode();

  /**
   * @brief Handle command list processing
   * Processes commands in command list mode
   * @param command The command to process
   */
  void handleCommandList(const String& command);

  /**
   * @brief Generate MPD OK response
   * @return OK response string
   */
  String mpdResponseOK();

  /**
   * @brief Generate MPD error response
   * @param message Error message
   * @return Error response string
   */
  String mpdResponseError(const String& command, const String& message);

  /**
   * @brief Send playlist information with configurable detail level
   * Sends playlist information with different levels of metadata
   * @param detailLevel 0=minimal (file+title), 1=simple (file+title+lastmod), 2=full (file+title+id+pos+lastmod)
   */
  void sendPlaylistInfo(int detailLevel);

  /**
   * @brief Handle MPD search/find commands
   * Processes search and find commands with partial or exact matching in stream names
   * @param command The full command string
   * @param exactMatch Whether to perform exact matching (find) or partial matching (search)
   */
  void handleMPDSearchCommand(const String& command, bool exactMatch);

  /**
   * @brief Handle MPD commands
   * Processes MPD protocol commands with support for MPD protocol version 0.23.0
   * @param command The command string to process
   * This function processes MPD protocol commands and controls the player accordingly.
   * It supports a subset of MPD commands including playback control, volume control,
   * playlist management, status queries, and search functionality.
   */
  void handleMPDCommand(const String& command);
};

#endif // MPD_H
