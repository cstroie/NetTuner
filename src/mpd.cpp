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

#include "mpd.h"
#include "main.h"  // For global function declarations

/**
 * @brief Parse value from string, handling quotes
 * @details Extracts a numeric value from a string, removing surrounding whitespace
 * and quotes if present. This is needed because MPD commands may send values
 * with or without quotes depending on the client implementation.
 * 
 * The function handles:
 * - Whitespace trimming
 * - Quote removal (both single and double quotes)
 * - String to integer conversion
 * 
 * @param valueStr The value string to parse
 * @return The parsed value as integer
 */
int parseValue(const String& valueStr) {
  String cleanedStr = valueStr;
  // Remove whitespace
  cleanedStr.trim();
  // Remove quotes if present
  if (cleanedStr.startsWith("\"") && cleanedStr.endsWith("\"") && cleanedStr.length() >= 2) {
    cleanedStr = cleanedStr.substring(1, cleanedStr.length() - 1);
  }
  // Convert to integer
  return cleanedStr.toInt();
}

/**
 * @brief Constructor for MPDInterface
 * @details Initializes the MPD interface with references to global variables
 * and the WiFi server instance. All parameters are passed by reference to
 * maintain direct access to the global state.
 * 
 * The constructor initializes all member variables with the provided references
 * and sets initial state for command processing.
 * 
 * @param server WiFiServer instance for MPD connections
 * @param streamTitle Reference to global stream title buffer
 * @param streamName Reference to global stream name buffer
 * @param streamURL Reference to global stream URL buffer
 * @param isPlaying Reference to global playing status flag
 * @param volume Reference to global volume level (0-22, ESP32-audioI2S scale)
 * @param bitrate Reference to global bitrate value (in kbps)
 * @param playlistCount Reference to global playlist count
 * @param currentSelection Reference to global current selection index
 * @param playlist Reference to global playlist array
 * @param audio Reference to global audio instance
 */
MPDInterface::MPDInterface(WiFiServer& server, char* streamTitle, char* streamName, char* streamURL,
               volatile bool& isPlaying, int& volume, int& bitrate, int& playlistCount,
               int& currentSelection, StreamInfo* playlist, Audio*& audio)
    : mpdServer(server), streamTitleRef(streamTitle), streamNameRef(streamName),
      streamURLRef(streamURL), isPlayingRef(isPlaying), volumeRef(volume), bitrateRef(bitrate),
      playlistCountRef(playlistCount), currentSelectionRef(currentSelection),
      playlistRef(playlist), audioRef(audio) {}

/**
 * @brief Handle MPD client connections and process commands
 * @details Manages the MPD protocol connection state and command processing.
 * 
 * This function handles new client connections, processes incoming MPD commands,
 * and manages special modes like command lists and idle mode. It also handles
 * client disconnections and ensures proper cleanup.
 * 
 * Connection handling:
 * - Accepts new connections when no client is connected
 * - Rejects new connections when a client is already connected
 * - Properly closes disconnected clients
 * 
 * In idle mode, the function monitors for changes in playback status and stream
 * information, sending notifications to clients when changes occur.
 * 
 * The function implements proper resource management:
 * - Ensures previous clients are properly closed before accepting new ones
 * - Resets command processing state on new connections
 * - Handles unexpected disconnections gracefully
 */
void MPDInterface::handleClient() {
    // Handle new client connections
    if (mpdServer.hasClient()) {
      if (!mpdClient || !mpdClient.connected()) {
        if (mpdClient) {
          mpdClient.stop();  // Ensure previous client is properly closed
        }
        mpdClient = mpdServer.available();
        // Send MPD welcome message
        if (mpdClient && mpdClient.connected()) {
          mpdClient.print("OK MPD 0.23.0\n");
        }
        // Reset command list state when new client connects
        inCommandList = false;
        commandListOK = false;
        commandListCount = 0;
        inIdleMode = false;
        commandBuffer = "";
      } else {
        // Reject connection if we already have a client
        WiFiClient rejectedClient = mpdServer.available();
        rejectedClient.stop();  // Properly close rejected connection
      }
    }
    // Check if client disconnected unexpectedly
    if (mpdClient && !mpdClient.connected()) {
      mpdClient.stop();
      // Reset command list state when client disconnects
      inCommandList = false;
      commandListOK = false;
      commandListCount = 0;
      inIdleMode = false;
      commandBuffer = "";
      return;
    }
    // Process client if connected
    if (mpdClient && mpdClient.connected()) {
      // Handle idle mode
      if (inIdleMode) {
        // Check for changes that would trigger idle notifications
        handleIdleMode();
        return;
      }
      // Handle incoming commands asynchronously
      handleAsyncCommands();
    }
}

/**
 * @brief Handle idle mode monitoring and notifications
 * @details Monitors for changes in playback status and stream information using
 * hash-based change detection for efficient monitoring.
 * 
 * Change detection works by computing hash values of the monitored data:
 * - Title hash: Computed from the stream title string using polynomial rolling hash
 * - Status hash: Computed from playing status, volume, and bitrate
 * 
 * When changes are detected, appropriate MPD idle notifications are sent:
 * - "changed: playlist" for title changes
 * - "changed: player" and "changed: mixer" for status changes
 * 
 * The function also handles the noidle command to exit idle mode.
 * 
 * Hash computation uses a simple polynomial rolling hash with base 31 for good
 * distribution properties while being computationally efficient.
 */
void MPDInterface::handleIdleMode() {
  // Check for title changes using hash computation
  unsigned long currentTitleHash = 0;
  for (int i = 0; streamTitleRef[i]; i++) {
    currentTitleHash = currentTitleHash * 31 + streamTitleRef[i];
  }
  
  // Check for status changes using hash computation
  unsigned long currentStatusHash = isPlayingRef ? 1 : 0;
  currentStatusHash = currentStatusHash * 31 + volumeRef;
  currentStatusHash = currentStatusHash * 31 + bitrateRef;
  
  bool sendIdleResponse = false;
  String idleChanges = "";
  
  // Check for title change
  if (currentTitleHash != lastTitleHash) {
    idleChanges += "changed: playlist\n";
    lastTitleHash = currentTitleHash;
    sendIdleResponse = true;
  }
  
  // Check for status change
  if (currentStatusHash != lastStatusHash) {
    idleChanges += "changed: player\n";
    idleChanges += "changed: mixer\n";
    lastStatusHash = currentStatusHash;
    sendIdleResponse = true;
  }
  
  // Send idle response if there are changes
  if (sendIdleResponse) {
    mpdClient.print(idleChanges);
    mpdClient.print(mpdResponseOK());
    inIdleMode = false;
    return;
  }
  
  // Check if there's data available (for noidle command)
  if (mpdClient.available()) {
    String command = mpdClient.readStringUntil('\n');
    command.trim();
    Serial.println("MPD Command: " + command);
    // Handle noidle command to exit idle mode
    if (command == "noidle") {
      inIdleMode = false;
      mpdClient.print(mpdResponseOK());
    }
  }
}
/**
 * @brief Handle playback command
 * @details Common handler for play and playid commands to reduce code duplication.
 * This function handles the actual playback logic for both commands, including
 * starting the stream, updating state, and sending responses.
 * 
 * @param index The playlist index to play (-1 for current selection)
 * @return true if playback started successfully, false otherwise
 */
bool MPDInterface::handlePlayback(int index) {
  if (playlistCountRef <= 0) {
    return false;
  }
  
  // Validate that we have a valid index within playlist bounds
  if (index < 0 || index >= playlistCountRef) {
    return false;
  }
  
  // Update current selection
  currentSelectionRef = index;
  
  // Start playback
  startStream(playlistRef[index].url, playlistRef[index].name);
  
  // Update state
  markPlayerStateDirty();
  savePlayerState();
  
  return true;
}

/**
 * @brief Handle command list processing
 * @details Processes commands in command list mode with support for both
 * command_list_begin and command_list_ok_begin modes.
 * 
 * In command_list_begin mode, commands are buffered until command_list_end
 * is received, then all commands are executed sequentially.
 * 
 * In command_list_ok_begin mode, each command receives a "list_OK" response
 * except the last which receives a standard "OK" response.
 * 
 * The function implements a safety limit of 50 commands to prevent memory issues
 * and potential denial of service attacks.
 * 
 * Command list processing follows the MPD protocol specification:
 * - Commands are executed in order
 * - Each command is processed as if sent individually
 * - Error in any command stops processing and returns error
 * 
 * @param command The command to process
 */
void MPDInterface::handleCommandList(const String& command) {
  if (command == "command_list_end") {
    // Execute all buffered commands
    for (int i = 0; i < commandListCount; i++) {
      handleMPDCommand(commandList[i]);
    }
    // Reset command list state
    inCommandList = false;
    commandListOK = false;
    commandListCount = 0;
    mpdClient.print(mpdResponseOK());
  } else {
    // Buffer the command
    if (commandListCount < 50) {
      commandList[commandListCount] = command;
      commandListCount++;
    } else {
      // Command list too long, send error
      inCommandList = false;
      commandListOK = false;
      commandListCount = 0;
      mpdClient.print(mpdResponseError("command_list", "Command list too long"));
    }
  }
}

/**
 * @brief Generate MPD OK response
 * @details Generates the appropriate OK response based on the current mode:
 * - In normal mode: returns "OK\n"
 * - In command list mode with list_OK enabled: returns "list_OK\n" for intermediate responses
 * - In command list mode with list_OK disabled: returns empty string for intermediate responses
 * 
 * The function follows MPD protocol specification for response formatting:
 * - Normal responses end with "\n"
 * - Command list responses follow specific sequencing rules
 * - Proper handling of intermediate vs final responses in command lists
 * 
 * @return OK response string
 */
String MPDInterface::mpdResponseOK() {
  // Check if in command list mode
  if (inCommandList) {
    // Send OK for each command if in command_list_ok_begin mode
    if (commandListOK) {
      return "list_OK\n";
    }
    else
      return "";
  }
  // Standard OK response
  return "OK\n";
}

/**
 * @brief Generate MPD error response
 * @details Generates a properly formatted MPD error response following the
 * MPD protocol specification: ACK [error_code@command_list_num] {current_command} message
 * 
 * This implementation uses error code 5 (ACK_ERROR_NO_EXIST) and command list
 * number 0 as these are appropriate for most general errors.
 * 
 * Error response format follows MPD specification:
 * - Error code 5 indicates "No such song"
 * - Command list number 0 indicates error in current command
 * - Current command name is included in braces
 * - Human-readable message follows the command name
 * 
 * @param command The command that caused the error
 * @param message Error message
 * @return Error response string in MPD format
 */
String MPDInterface::mpdResponseError(const String& command, const String& message) {
  return "ACK [5@0] {" + command + "} " + message + "\n";
}

/**
 * @brief Send playlist information with configurable detail level
 * @details Sends playlist information with different levels of metadata based
 * on the detail level parameter. Higher detail levels include more metadata fields.
 * 
 * Detail levels:
 * - 0: Minimal (file+title) - for listplaylistinfo
 * - 1: Simple (file+title+lastmod) - for lsinfo, listallinfo
 * - 2: Full (file+title+id+pos+lastmod) - for playlistinfo
 * - 3: Artist/Album (file+title+artist+album+id+pos+lastmod) - for search/find with artist/album
 * 
 * For artist/album detail levels, dummy "WebRadio" values are used since
 * web radio streams don't have traditional artist/album metadata.
 * 
 * Metadata fields included per detail level:
 * - Level 0: file, Title
 * - Level 1: file, Title, Track, Last-Modified
 * - Level 2: file, Title, Id, Pos, Track, Last-Modified
 * - Level 3: file, Title, Artist, Album, Id, Pos, Track, Last-Modified
 * 
 * @param detailLevel 
 * 0=minimal (file+title), 
 * 1=simple (file+title+lastmod), 
 * 2=full (file+title+id+pos+lastmod), 
 * 3=artist/album (file+title+artist+album+id+pos+lastmod)
 */
void MPDInterface::sendPlaylistInfo(int detailLevel) {
  for (int i = 0; i < playlistCountRef; i++) {
    mpdClient.print("file: " + String(playlistRef[i].url) + "\n");
    mpdClient.print("Title: " + String(playlistRef[i].name) + "\n");
    if (detailLevel >= 3) {
      // Artist/Album detail level
      mpdClient.print("Artist: WebRadio\n");
      mpdClient.print("Album: WebRadio\n");
    }
    if (detailLevel >= 2) {
      // Full detail level
      mpdClient.print("Id: " + String(i) + "\n");
      mpdClient.print("Pos: " + String(i) + "\n");
    }
    if (detailLevel >= 1) {
      // Simple detail level
      mpdClient.print("Track: " + String(i + 1) + "\n");
      mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
    }
  }
}

/**
 * @brief Handle MPD search/find commands
 * @details Processes search and find commands with partial or exact matching in stream names.
 * The function parses the search criteria and filters the playlist entries based on
 * the specified matching mode (partial for search, exact for find).
 * 
 * Command format examples:
 * - search "Title" "search term"
 * - find "Artist" "exact artist name"
 * 
 * Special handling is implemented for artist/album searches which return
 * simple playlist information rather than filtered results.
 * 
 * Search is case-insensitive and handles quoted strings properly.
 * 
 * The function implements proper parsing of search commands:
 * - Extracts search filter and term from command string
 * - Handles quoted strings with proper quote removal
 * - Performs case-insensitive comparison
 * - Supports both partial (search) and exact (find) matching
 * 
 * @param command The full command string
 * @param exactMatch Whether to perform exact matching (find) or partial matching (search)
 */
void MPDInterface::handleMPDSearchCommand(const String& command, bool exactMatch) {
  // Determine command prefix length (search=6, find=4)
  int prefixLength = command.startsWith("search") ? 6 : 4;
  if (command.length() > prefixLength + 1) {
    // Extract search filter and term
    String searchFilter = command.substring(prefixLength + 1);
    searchFilter.trim();
    String searchTerm;
    // Extract search filter (after the first space and before the next space)
    int firstSpace = searchFilter.indexOf(' ');
    if (firstSpace != -1) {
      // Extract search term (everything after the next space)
      searchTerm = searchFilter.substring(firstSpace + 1);
      searchTerm.trim();
      // Extract search filter (everything before the next space)
      searchFilter = searchFilter.substring(0, firstSpace);
      searchFilter.trim();
    }
    // Remove quotes if present
    if (searchFilter.startsWith("\"") && searchFilter.endsWith("\"") && searchFilter.length() >= 2) {
      searchFilter = searchFilter.substring(1, searchFilter.length() - 1);
    }
    if (searchTerm.startsWith("\"") && searchTerm.endsWith("\"") && searchTerm.length() >= 2) {
      searchTerm = searchTerm.substring(1, searchTerm.length() - 1);
    }
    // Handle special cases for artist/album searches
    if (searchFilter == "album" || searchFilter == "artist") {
      sendPlaylistInfo(1); // Send simple info for album search
      return;
    }
    // Search in playlist names
    for (int i = 0; i < playlistCountRef; i++) {
      String playlistName = String(playlistRef[i].name);
      // Convert both to lowercase for case-insensitive comparison
      String lowerName = playlistName;
      lowerName.toLowerCase();
      String lowerSearch = searchTerm;
      lowerSearch.toLowerCase();
      bool match = false;
      if (exactMatch) {
        // Exact match for find command
        match = (lowerName == lowerSearch);
      } else {
        // Partial match for search command
        match = (lowerName.indexOf(lowerSearch) != -1);
      }
      // If a match is found, send the metadata
      if (match) {
        mpdClient.print("file: " + String(playlistRef[i].url) + "\n");
        mpdClient.print("Title: " + String(playlistRef[i].name) + "\n");
        mpdClient.print("Track: " + String(i + 1) + "\n");
        mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
      }
    }
  }
}

/**
 * @brief Handle MPD commands
 * @details Processes MPD protocol commands with support for MPD protocol version 0.23.0.
 * This function processes MPD protocol commands and controls the player accordingly.
 * It supports a subset of MPD commands including playback control, volume control,
 * playlist management, status queries, and search functionality.
 * 
 * Command processing includes:
 * - Playback control (play, stop, pause, next, previous)
 * - Volume control (setvol, getvol, volume)
 * - Status queries (status, currentsong, stats)
 * - Playlist management (playlistinfo, playlistid, lsinfo, listallinfo, listplaylistinfo)
 * - Search functionality (search, find)
 * - System commands (ping, commands, notcommands, tagtypes, outputs)
 * - Special modes (idle, noidle, command lists)
 * 
 * Volume handling converts between MPD's 0-100 scale and the ESP32-audioI2S 0-22 scale.
 * 
 * The function implements comprehensive command handling:
 * - Validates command parameters
 * - Converts between MPD and internal value ranges
 * - Updates global state variables
 * - Notifies WebSocket clients of state changes
 * - Follows MPD protocol response format
 * 
 * @param command The command string to process
 * 
 * Supported commands include:
 * - Playback: play, stop, pause, next, previous
 * - Volume: setvol, getvol, volume
 * - Status: status, currentsong, stats
 * - Playlist: playlistinfo, playlistid, lsinfo, listallinfo, listplaylistinfo
 * - Search: search, find
 * - System: ping, commands, notcommands, tagtypes, outputs
 * - Special modes: idle, noidle, command lists
 */
void MPDInterface::handleMPDCommand(const String& command) {
  if (command.startsWith("stop") || command.startsWith("pause")) {
    // Stop/Pause command (both treated as stop for simplicity)
    stopStream();
    markPlayerStateDirty();
    savePlayerState();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("status")) {
    // Status command
    // Convert volume from 0-22 scale to 0-100 scale for MPD compatibility
    int volPercent = map(volumeRef, 0, 22, 0, 100);
    mpdClient.print("volume: " + String(volPercent) + "\n");
    mpdClient.print("repeat: 0\n");
    mpdClient.print("random: 0\n");
    mpdClient.print("single: 0\n");
    mpdClient.print("consume: 0\n");
    mpdClient.print("playlist: 1\n");
    mpdClient.print("playlistlength: " + String(playlistCountRef) + "\n");
    mpdClient.print("mixrampdb: 0.000000\n");
    mpdClient.print("state: " + String(isPlayingRef ? "play" : "stop") + "\n");
    if (isPlayingRef && strlen(streamNameRef) > 0) {
      mpdClient.print("song: " + String(currentSelectionRef) + "\n");
      mpdClient.print("songid: " + String(currentSelectionRef) + "\n");
      // Calculate elapsed time since playback started
      extern unsigned long playStartTime;
      unsigned long elapsed = 0;
      if (playStartTime > 0) {
        elapsed = (millis() / 1000) - playStartTime;
      }
      mpdClient.print("elapsed: " + String(elapsed) + ".000\n");
      mpdClient.print("bitrate: " + String(bitrateRef) + "\n");
      mpdClient.print("audio: 44100:16:2\n");
      mpdClient.print("nextsong: " + String((currentSelectionRef + 1) % playlistCountRef) + "\n");
      mpdClient.print("nextsongid: " + String((currentSelectionRef + 1) % playlistCountRef) + "\n");
    }
    mpdClient.print("updating_db: 0\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("currentsong")) {
    // Current song command
    if (isPlayingRef && strlen(streamNameRef) > 0) {
      mpdClient.print("file: " + String(streamURLRef) + "\n");
      if (strlen(streamTitleRef) > 0) {
        String streamTitleStr = String(streamTitleRef);
        // Check if stream title contains " - " separator for artist/track parsing
        int separatorPos = streamTitleStr.indexOf(" - ");
        if (separatorPos != -1) {
          // Split into artist and track using the " - " separator
          String artist = streamTitleStr.substring(0, separatorPos);
          String title = streamTitleStr.substring(separatorPos + 3); // Skip " - "
          mpdClient.print("Artist: " + artist + "\n");
          mpdClient.print("Title: " + title + "\n");
        } else {
          // No separator, use full title as track name
          mpdClient.print("Title: " + streamTitleStr + "\n");
        }
      } else {
        // No stream title, use stream name as fallback
        mpdClient.print("Title: " + String(streamNameRef) + "\n");
      }
      mpdClient.print("Id: " + String(currentSelectionRef) + "\n");
      mpdClient.print("Pos: " + String(currentSelectionRef) + "\n");
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistinfo")) {
    // Playlist info command
    sendPlaylistInfo(3);
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistid")) {
    // Playlist ID command
    int id = -1;
    if (command.length() > 10) {
      id = parseValue(command.substring(11));
    }
    // Check if the ID is valid
    if (id >= 0 && id < playlistCountRef) {
      mpdClient.print("file: " + String(playlistRef[id].url) + "\n");
      mpdClient.print("Title: " + String(playlistRef[id].name) + "\n");
      mpdClient.print("Artist: WebRadio\n");
      mpdClient.print("Album: WebRadio\n");
      mpdClient.print("Id: " + String(id) + "\n");
      mpdClient.print("Pos: " + String(id) + "\n");
    } else {
      // Return all if no specific ID
      sendPlaylistInfo(3);
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playid") || command.startsWith("play")) {
    // Play and Play ID commands
    int prefixLength = command.startsWith("playid") ? 6 : 4;
    int playlistIndex = -1;
    if (command.length() > prefixLength + 1) {
      playlistIndex = parseValue(command.substring(prefixLength + 1));
    }
    
    if (handlePlayback(playlistIndex)) {
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("play", "No playlist"));
    }
  } else if (command.startsWith("lsinfo")) {
    // List info command
    sendPlaylistInfo(1); // Simple detail
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("setvol")) {
    // Set volume command
    if (command.length() > 7) {
      String volumeStr = command.substring(7);
      // Parse volume value, handling quotes if present
      int newVolume = parseValue(volumeStr);
      if (newVolume >= 0 && newVolume <= 100) {
        // Convert from MPD's 0-100 scale to ESP32-audioI2S 0-22 scale
        volumeRef = map(newVolume, 0, 100, 0, 22);
        if (audioRef) {
          audioRef->setVolume(volumeRef);  // ESP32-audioI2S uses 0-22 scale
        }
        updateDisplay();
        sendStatusToClients();  // Notify WebSocket clients of volume change
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("setvol", "Volume out of range"));
      }
    } else {
      mpdClient.print(mpdResponseError("setvol", "Missing volume value"));
    }
  } else if (command.startsWith("getvol")) {
    // Get volume command
    int volPercent = map(volumeRef, 0, 22, 0, 100);
    mpdClient.print("volume: " + String(volPercent) + "\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("volume")) {
    // Volume command - change volume by relative amount
    if (command.length() > 7) {
      String volumeStr = command.substring(7);
      // Parse volume change value (can be negative for decrease)
      int volumeChange = parseValue(volumeStr);
      
      // Get current volume as percentage for MPD compatibility
      int currentVolPercent = map(volumeRef, 0, 22, 0, 100);
      
      // Apply change and clamp to 0-100 range
      int newVolPercent = currentVolPercent + volumeChange;
      if (newVolPercent < 0) newVolPercent = 0;
      if (newVolPercent > 100) newVolPercent = 100;
      
      // Convert back to 0-22 scale for ESP32-audioI2S and set
      volumeRef = map(newVolPercent, 0, 100, 0, 22);
      if (audioRef) {
        audioRef->setVolume(volumeRef);  // ESP32-audioI2S uses 0-22 scale
      }
      updateDisplay();
      sendStatusToClients();  // Notify WebSocket clients of volume change
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("volume", "Missing volume change value"));
    }
  } else if (command.startsWith("next")) {
    // Next command
    if (playlistCountRef > 0) {
      int nextIndex = (currentSelectionRef + 1) % playlistCountRef;
      if (handlePlayback(nextIndex)) {
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("next", "Playback failed"));
      }
    } else {
      mpdClient.print(mpdResponseError("next", "No playlist"));
    }
  } else if (command.startsWith("previous")) {
    // Previous command
    if (playlistCountRef > 0) {
      int prevIndex = (currentSelectionRef - 1 + playlistCountRef) % playlistCountRef;
      if (handlePlayback(prevIndex)) {
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("previous", "Playback failed"));
      }
    } else {
      mpdClient.print(mpdResponseError("previous", "No playlist"));
    }
  } else if (command.startsWith("clear")) {
    // Clear command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("add")) {
    // Add command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("delete")) {
    // Delete command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("load")) {
    // Load command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("save")) {
    // Save command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("outputs")) {
    // Outputs command - with ESP32-audioI2S, we only have I2S output
    mpdClient.print("outputid: 0\n");
    mpdClient.print("outputname: I2S (External DAC)\n");
    mpdClient.print("outputenabled: 1\n");
    //mpdClient.print("attribute: allowed_formats=\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("disableoutput")) {
    // Disable output command
    if (command.length() > 13) {
      int outputId = parseValue(command.substring(14));
      // We don't actually disable outputs, just acknowledge the command
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("disableoutput", "Invalid output ID"));
    }
  } else if (command.startsWith("enableoutput")) {
    // Enable output command
    if (command.length() > 12) {
      int outputId = parseValue(command.substring(13));
      if (outputId == 0) {
        // Only output 0 (I2S) is supported with ESP32-audioI2S
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("enableoutput", "Invalid output ID"));
      }
    } else {
      mpdClient.print(mpdResponseError("enableoutput", "Missing output ID"));
    }
  } else if (command.startsWith("commands")) {
    // Commands command
    // Send the list of supported commands
    for (int i = 0; i < SUPPORTED_COMMANDS_COUNT; i++) {
      mpdClient.print("command: ");
      mpdClient.print(supportedCommands[i]);
      mpdClient.print("\n");
    }
    // End of command list
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("notcommands")) {
    // Not commands command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("stats")) {
    // Stats command
    unsigned long uptime = (millis() / 1000) - startTime;
    unsigned long playtime = totalPlayTime;
    if (isPlayingRef && playStartTime > 0) {
      playtime += (millis() / 1000) - playStartTime;
    }
    // Send stats information
    mpdClient.print("artists: 1\n");
    mpdClient.print("albums: 1\n");
    mpdClient.print("songs: " + String(playlistCountRef) + "\n");
    mpdClient.print("uptime: " + String(uptime) + "\n");
    mpdClient.print("playtime: " + String(playtime) + "\n");
    mpdClient.print("db_playtime: " + String(playtime) + "\n");
    mpdClient.print("db_update: " + String(BUILD_TIME) + "\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("ping")) {
    // Ping command
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("password")) {
    // Password command (not implemented)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("kill")) {
    // Kill command (not implemented for this player)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("update")) {
    // Update command (not implemented for this player)
    mpdClient.print("updating_db: 1\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("listallinfo")) {
    // List all info command
    sendPlaylistInfo(1); // Simple detail
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("listplaylistinfo")) {
    // List playlist info command
    sendPlaylistInfo(0); // Minimal detail
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("listplaylists")) {
    // List playlists command
    // For this implementation, we only have one playlist (the main playlist)
    mpdClient.print("playlist: WebRadio\n");
    mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("list")) {
    // List command
    if (command.length() > 5) {
      String tagType = command.substring(5);
      tagType.toLowerCase();
      String tag = "Title: ";
      tagType.trim();
      if (tagType.startsWith("artist")) {
        tag = "Artist: ";
        mpdClient.print("Artist: WebRadio\n");
      } else if (tagType.startsWith("album")) {
        tag = "Album: ";
        mpdClient.print("Album: WebRadio\n");
      } else if (tagType.startsWith("title")) {
        tag = "Title: ";
        // Return the playlist
        for (int i = 0; i < playlistCountRef; i++) {
          mpdClient.print(tag + String(playlistRef[i].name) + "\n");
        }
      }
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("search")) {
    // Search command (partial match)
    handleMPDSearchCommand(command, false);
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("find")) {
    // Find command (exact match)
    handleMPDSearchCommand(command, true);
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("seek")) {
    // Seek command (not implemented for streaming)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("seekid")) {
    // Seek ID command (not implemented for streaming)
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("tagtypes")) {
    // Tag types command
    if (command.equals("tagtypes \"all\"") || command.equals("tagtypes \"clear\"")) {
      // These commands simply return OK
      mpdClient.print(mpdResponseOK());
    } else {
      // Default tagtypes response
      const char* supportedTagTypes[] = {
        "Artist",
        "Album",
        "Title",
        "Track",
        "Name",
        "Genre",
        "Date",
        "Composer",
        "Performer",
        "Comment",
        "Disc",
        "MUSICBRAINZ_ARTISTID",
        "MUSICBRAINZ_ALBUMID",
        "MUSICBRAINZ_ALBUMARTISTID",
        "MUSICBRAINZ_TRACKID"
      };
      // Send the list of supported tag types
      const int tagTypeCount = sizeof(supportedTagTypes) / sizeof(supportedTagTypes[0]);
      for (int i = 0; i < tagTypeCount; i++) {
        mpdClient.print("tagtype: ");
        mpdClient.print(supportedTagTypes[i]);
        mpdClient.print("\n");
      }
      mpdClient.print(mpdResponseOK());
    }
  } else if (command.startsWith("plchanges")) {
    // Playlist changes command
    // For simplicity, we'll return the entire playlist (as if all entries changed)
    sendPlaylistInfo(2);
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("idle")) {
    // Idle command - enter idle mode and wait for changes
    inIdleMode = true;
    // Initialize hashes for tracking changes
    lastTitleHash = 0;
    for (int i = 0; streamTitleRef[i]; i++) {
      lastTitleHash = lastTitleHash * 31 + streamTitleRef[i];
    }
    lastStatusHash = isPlayingRef ? 1 : 0;
    lastStatusHash = lastStatusHash * 31 + volumeRef;
    lastStatusHash = lastStatusHash * 31 + bitrateRef;
    // Don't send immediate response - wait for changes
    return;
  } else if (command.startsWith("noidle")) {
    // Noidle command
    inIdleMode = false;
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("close")) {
    // Close command
    mpdClient.print(mpdResponseOK());
    mpdClient.stop();
  } else if (command.startsWith("command_list_begin")) {
    // Start command list mode
    inCommandList = true;
    commandListOK = false;
    commandListCount = 0;
    // Don't send OK yet, wait for command_list_end
  } else if (command.startsWith("command_list_ok_begin")) {
    // Start command list mode with OK responses
    inCommandList = true;
    commandListOK = true;
    commandListCount = 0;
    // Don't send OK yet, wait for command_list_end
  } else if (command.startsWith("command_list_end")) {
    // This should not happen outside of command list mode
    mpdClient.print(mpdResponseError("command_list", "Not in command list mode"));
  } else if (command.startsWith("decoders")) {
    // Decoders command - return supported audio decoders
    // ESP32-audioI2S supports these formats
    mpdClient.print("plugin: mp3\n");
    mpdClient.print("suffix: mp3\n");
    mpdClient.print("mime_type: audio/mpeg\n");
    mpdClient.print(mpdResponseOK());
  } else if (command.length() == 0) {
    // Empty command
    mpdClient.print(mpdResponseOK());
  } else {
    // Unknown command
    mpdClient.print(mpdResponseError("unknown", "Unknown command"));
  }
}

/**
 * @brief Handle asynchronous command processing
 * @details Processes commands without blocking, allowing for better responsiveness.
 * This function reads available data from the client connection and accumulates
 * it in a buffer until a complete command (terminated by newline) is received.
 * It then processes the command according to the current mode (normal, command list, etc.).
 * 
 * The function processes only one command per call to avoid blocking other operations,
 * which is important for maintaining responsive MPD service.
 */
void MPDInterface::handleAsyncCommands() {
    // Read available data without blocking
    while (mpdClient.available()) {
        char c = mpdClient.read();
        if (c == '\n') {
            // Process complete command
            String command = commandBuffer;
            command.trim();
            commandBuffer = "";
            
            if (command.length() > 0) {
                Serial.println("MPD Command: " + command);
                // Handle command list mode
                if (inCommandList) {
                    handleCommandList(command);
                } else {
                    // Normal command processing
                    handleMPDCommand(command);
                }
            }
            break; // Process one command at a time to avoid blocking
        } else {
            commandBuffer += c;
        }
    }
}
