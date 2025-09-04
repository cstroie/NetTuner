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
 * @details Encapsulates all MPD protocol functionality for the NetTuner
 * This class handles MPD client connections, command processing, and protocol
 * compliance for controlling the internet radio player.
 */
class MPDInterface {
private:
  WiFiServer& mpdServer;             ///< WiFi server instance for MPD connections
  WiFiClient mpdClient;              ///< Current MPD client connection

  // References to global variables
  char* streamTitleRef;              ///< Reference to global stream title buffer
  char* streamNameRef;               ///< Reference to global stream name buffer
  char* streamURLRef;                ///< Reference to global stream URL buffer
  volatile bool& isPlayingRef;       ///< Reference to global playing status flag
  int& volumeRef;                    ///< Reference to global volume level (0-22)
  int& bitrateRef;                   ///< Reference to global bitrate value
  int& playlistCountRef;             ///< Reference to global playlist count
  int& currentSelectionRef;          ///< Reference to global current selection index
  StreamInfo* playlistRef;           ///< Reference to global playlist array
  Audio*& audioRef;                  ///< Reference to global audio instance

  // MPD command list state variables
  bool inCommandList = false;        ///< Flag indicating if we're in command list mode
  bool commandListOK = false;        ///< Flag indicating if we should send list_OK responses
  String commandList[50];            ///< Buffer for command list (max 50 commands)
  int commandListCount = 0;          ///< Number of commands in the current command list

  // MPD idle state variables
  bool inIdleMode = false;           ///< Flag indicating if we're in idle mode
  unsigned long lastTitleHash = 0;   ///< Hash of last stream title for change detection
  unsigned long lastStatusHash = 0;  ///< Hash of last status for change detection

  // Asynchronous command handling buffer
  String commandBuffer = "";         ///< Buffer for accumulating incoming commands

public:
  MPDInterface(WiFiServer& server, char* streamTitle, char* streamName, char* streamURL,
               volatile bool& isPlaying, int& volume, int& bitrate, int& playlistCount,
               int& currentSelection, StreamInfo* playlist, Audio*& audio);

  /**
   * @brief Handle MPD client connections and process commands
   * @details Manages the MPD protocol connection state and command processing.
   * This function handles new client connections, processes incoming MPD commands,
   * and manages special modes like command lists and idle mode. It also handles
   * client disconnections and ensures proper cleanup.
   * 
   * In idle mode, the function monitors for changes in playback status and stream
   * information, sending notifications to clients when changes occur.
   */
  void handleClient();

private:
  /**
   * @brief Handle asynchronous command processing
   * @details Processes commands without blocking, allowing for better responsiveness.
   * This function reads available data from the client connection and accumulates
   * it in a buffer until a complete command (terminated by newline) is received.
   * It then processes the command according to the current mode (normal, command list, etc.).
   */
  void handleAsyncCommands();

  /**
   * @brief Handle idle mode monitoring and notifications
   * @details Monitors for changes in playback status and stream information.
   * In idle mode, the client is waiting for notifications about changes in the
   * player state. This function uses hash-based change detection to efficiently
   * monitor for changes in stream title and playback status, sending appropriate
   * notifications when changes occur.
   */
  void handleIdleMode();

  /**
   * @brief Handle command list processing
   * @details Processes commands in command list mode.
   * In command list mode, multiple commands are sent as a batch and executed
   * together. This function buffers commands until the end of the list is
   * received, then executes all commands in sequence.
   * @param command The command to process
   */
  void handleCommandList(const String& command);

  /**
   * @brief Generate MPD OK response
   * @details Generates the appropriate OK response based on the current mode.
   * In normal mode, returns "OK\n". In command list mode with list_OK enabled,
   * returns "list_OK\n" for intermediate responses and "OK\n" for the final response.
   * @return OK response string
   */
  String mpdResponseOK();

  /**
   * @brief Generate MPD error response
   * @details Generates a properly formatted MPD error response with error code,
   * position, command name, and error message.
   * @param command The command that caused the error
   * @param message Error message
   * @return Error response string in MPD format
   */
  String mpdResponseError(const String& command, const String& message);

  /**
   * @brief Send playlist information with configurable detail level
   * @details Sends playlist information with different levels of metadata based
   * on the detail level parameter. Higher detail levels include more metadata fields.
   * @param detailLevel 
   * 0=minimal (file+title), 
   * 1=simple (file+title+lastmod), 
   * 2=full (file+title+id+pos+lastmod), 
   * 3=artist/album (file+title+artist+album+id+pos+lastmod)
   */
  void sendPlaylistInfo(int detailLevel);

  /**
   * @brief Handle MPD search/find commands
   * @details Processes search and find commands with partial or exact matching in stream names.
   * The function parses the search criteria and filters the playlist entries based on
   * the specified matching mode (partial for search, exact for find).
   * @param command The full command string
   * @param exactMatch Whether to perform exact matching (find) or partial matching (search)
   */
  void handleMPDSearchCommand(const String& command, bool exactMatch);

  /**
   * @brief Handle MPD commands
   * @details Processes MPD protocol commands with support for MPD protocol version 0.23.0.
   * This function processes MPD protocol commands and controls the player accordingly.
   * It supports a subset of MPD commands including playback control, volume control,
   * playlist management, status queries, and search functionality.
   * 
   * Supported commands include:
   * - Playback: play, stop, pause, next, previous
   * - Volume: setvol, getvol, volume
   * - Status: status, currentsong, stats
   * - Playlist: playlistinfo, playlistid, lsinfo, listallinfo, listplaylistinfo
   * - Search: search, find
   * - System: ping, commands, notcommands, tagtypes, outputs
   * - Special modes: idle, noidle, command lists
   * @param command The command string to process
   */
  void handleMPDCommand(const String& command);
};

#endif // MPD_H
