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

extern void stopStream();
extern void startStream(const char* url, const char* name);
extern void updateDisplay();
extern void sendStatusToClients();
extern const char* BUILD_TIME;
extern unsigned long startTime;
extern unsigned long totalPlayTime;
extern unsigned long playStartTime;

// Define mpdClient as a global variable
WiFiClient mpdClient;

MPDInterface::MPDInterface(WiFiServer& server, char* streamTitle, char* streamName, char* streamURL,
               volatile bool& isPlaying, int& volume, int& bitrate, int& playlistCount,
               int& currentSelection, StreamInfo* playlist, Audio*& audio)
    : mpdServer(server), streamTitleRef(streamTitle), streamNameRef(streamName),
      streamURLRef(streamURL), isPlayingRef(isPlaying), volumeRef(volume), bitrateRef(bitrate),
      playlistCountRef(playlistCount), currentSelectionRef(currentSelection),
      playlistRef(playlist), audioRef(audio) {}

/**
 * @brief Handle MPD client connections and process commands
 * Manages the MPD protocol connection state and command processing
 * 
 * This function handles new client connections, processes incoming MPD commands,
 * and manages special modes like command lists and idle mode. It also handles
 * client disconnections and ensures proper cleanup.
 * 
 * In idle mode, the function monitors for changes in playback status and stream
 * information, sending notifications to clients when changes occur.
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
      // Handle incoming commands
      if (mpdClient.available()) {
        String command = mpdClient.readStringUntil('\n');
        command.trim();
        Serial.println("MPD Command: " + command);
        // Handle command list mode
        if (inCommandList) {
          handleCommandList(command);
        } else {
          // Normal command processing
          handleMPDCommand(command);
        }
      }
    }
}

/**
 * @brief Handle idle mode monitoring and notifications
 * Monitors for changes in playback status and stream information
 */
void MPDInterface::handleIdleMode() {
  // Check for title changes
  unsigned long currentTitleHash = 0;
  for (int i = 0; streamTitleRef[i]; i++) {
    currentTitleHash = currentTitleHash * 31 + streamTitleRef[i];
  }
  // Check for status changes
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
 * @brief Handle command list processing
 * Processes commands in command list mode
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
      mpdClient.print(mpdResponseError("Command list too long"));
    }
  }
}


/**
 * @brief Generate appropriate MPD response based on context
 * @param isError Whether this is an error response
 * @param message Error message (only used for error responses)
 * @return Response string
 */
String MPDInterface::mpdResponse(bool isError, const String& message) {
  if (isError) {
    return "ACK [5@0] {} " + message + "\n";
  }
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
 * @brief Generate MPD OK response
 * @return OK response string
 */
String MPDInterface::mpdResponseOK() {
  return mpdResponse(false, "");
}

/**
 * @brief Generate MPD error response
 * @param message Error message
 * @return Error response string
 */
String MPDInterface::mpdResponseError(const String& message) {
  return mpdResponse(true, message);
}

/**
 * @brief Send playlist information with configurable detail level
 * Sends playlist information with different levels of metadata
 * @param detailLevel 0=minimal (file+title), 1=simple (file+title+lastmod), 2=full (file+title+id+pos+lastmod)
 */
void MPDInterface::sendPlaylistInfo(int detailLevel) {
  for (int i = 0; i < playlistCountRef; i++) {
    mpdClient.print("file: " + String(playlistRef[i].url) + "\n");
    mpdClient.print("Title: " + String(playlistRef[i].name) + "\n");
    if (detailLevel >= 2) {
      // Full detail level
      mpdClient.print("Id: " + String(i) + "\n");
      mpdClient.print("Pos: " + String(i) + "\n");
    }
    if (detailLevel >= 1) {
      // Simple detail level
      mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
    }
  }
}

/**
 * @brief Handle MPD search/find commands
 * Processes search and find commands with partial or exact matching in stream names
 * @param command The full command string
 * @param exactMatch Whether to perform exact matching (find) or partial matching (search)
 */
void MPDInterface::handleMPDSearchCommand(const String& command, bool exactMatch) {
  // Determine command prefix length (search=6, find=4)
  int prefixLength = command.startsWith("search") ? 6 : 4;
  if (command.length() > prefixLength + 1) {
    String searchTerm = command.substring(prefixLength + 1);
    searchTerm.trim();
    // Extract search string (everything after the first space)
    int firstSpace = searchTerm.indexOf(' ');
    if (firstSpace != -1) {
      searchTerm = searchTerm.substring(firstSpace + 1);
      searchTerm.trim();
    }
    // Remove quotes if present
    if (searchTerm.startsWith("\"") && searchTerm.endsWith("\"") && searchTerm.length() >= 2) {
      searchTerm = searchTerm.substring(1, searchTerm.length() - 1);
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
        mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
      }
    }
  }
}

/**
 * @brief Handle MPD commands
 * Processes MPD protocol commands with support for MPD protocol version 0.23.0
 * @param command The command string to process
 * 
 * This function processes MPD protocol commands and controls the player accordingly.
 * It supports a subset of MPD commands including playback control, volume control,
 * playlist management, status queries, and search functionality.
 * 
 * Supported commands include:
 * - Playback: play, stop, pause, next, previous
 * - Volume: setvol
 * - Status: status, currentsong, stats
 * - Playlist: playlistinfo, playlistid, lsinfo, listallinfo, listplaylistinfo
 * - Search: search, find
 * - System: ping, commands, notcommands, tagtypes, outputs
 * - Special modes: idle, noidle, command lists
 */
void MPDInterface::handleMPDCommand(const String& command) {
  if (command.startsWith("stop")) {
    // Stop command
    stopStream();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("pause")) {
    // Pause command (treat as stop for simplicity)
    stopStream();
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("status")) {
    // Status command
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
      mpdClient.print("time: 0:0\n");
      mpdClient.print("elapsed: 0.000\n");
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
        mpdClient.print("Title: " + String(streamTitleRef) + "\n");
      } else {
        mpdClient.print("Title: " + String(streamNameRef) + "\n");
      }
      mpdClient.print("Id: " + String(currentSelectionRef) + "\n");
      mpdClient.print("Pos: " + String(currentSelectionRef) + "\n");
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistinfo")) {
    // Playlist info command
    sendPlaylistInfo(2); // Full detail
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("playlistid")) {
    // Playlist ID command
    int id = -1;
    if (command.length() > 10) {
      id = command.substring(11).toInt();
    }
    // Check if the ID is valid
    if (id >= 0 && id < playlistCountRef) {
      mpdClient.print("file: " + String(playlistRef[id].url) + "\n");
      mpdClient.print("Title: " + String(playlistRef[id].name) + "\n");
      mpdClient.print("Id: " + String(id) + "\n");
      mpdClient.print("Pos: " + String(id) + "\n");
      mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
    } else {
      // Return all if no specific ID
      sendPlaylistInfo(2); // Full detail
    }
    mpdClient.print(mpdResponseOK());
  } else if (command.startsWith("lsinfo")) {
    // List info command
    sendPlaylistInfo(1); // Simple detail
    mpdClient.print(mpdResponseOK());
  } else  if (command.startsWith("play")) {
    // Play command
    if (playlistCountRef > 0) {
      int index = -1;
      if (command.length() > 5) {
        index = command.substring(5).toInt();
      }
      // If a valid index is found, play the selected track
      if (index >= 0 && index < playlistCountRef) {
        currentSelectionRef = index;
        startStream(playlistRef[index].url, playlistRef[index].name);
      } else if (playlistCountRef > 0 && currentSelectionRef < playlistCountRef) {
        startStream(playlistRef[currentSelectionRef].url, playlistRef[currentSelectionRef].name);
      }
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
    }
  } else if (command.startsWith("setvol")) {
    // Set volume command
    if (command.length() > 7) {
      String volumeStr = command.substring(7);
      volumeStr.trim();
      // Remove quotes if present
      if (volumeStr.startsWith("\"") && volumeStr.endsWith("\"") && volumeStr.length() >= 2) {
        volumeStr = volumeStr.substring(1, volumeStr.length() - 1);
      }
      // Convert to integer and set volume
      int newVolume = volumeStr.toInt();
      if (newVolume >= 0 && newVolume <= 100) {
        volumeRef = map(newVolume, 0, 100, 0, 22);  // Map 0-100 to 0-22 scale
        if (audioRef) {
          audioRef->setVolume(volumeRef);  // ESP32-audioI2S uses 0-22 scale
        }
        updateDisplay();
        sendStatusToClients();  // Notify WebSocket clients
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("Volume out of range"));
      }
    } else {
      mpdClient.print(mpdResponseError("Missing volume value"));
    }
  } else if (command.startsWith("next")) {
    // Next command
    if (playlistCountRef > 0) {
      currentSelectionRef = (currentSelectionRef + 1) % playlistCountRef;
      if (isPlayingRef) {
        startStream(playlistRef[currentSelectionRef].url, playlistRef[currentSelectionRef].name);
      }
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
    }
  } else if (command.startsWith("previous")) {
    // Previous command
    if (playlistCountRef > 0) {
      currentSelectionRef = (currentSelectionRef - 1 + playlistCountRef) % playlistCountRef;
      if (isPlayingRef) {
        startStream(playlistRef[currentSelectionRef].url, playlistRef[currentSelectionRef].name);
      }
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
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
      int outputId = command.substring(14).toInt();
      // We don't actually disable outputs, just acknowledge the command
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("Missing output ID"));
    }
  } else if (command.startsWith("enableoutput")) {
    // Enable output command
    if (command.length() > 12) {
      int outputId = command.substring(13).toInt();
      if (outputId == 0) {
        // Only output 0 (I2S) is supported with ESP32-audioI2S
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("Invalid output ID"));
      }
    } else {
      mpdClient.print(mpdResponseError("Missing output ID"));
    }
  } else if (command.startsWith("commands")) {
    // Commands command
    const char* supportedCommands[] = {
      "add",
      "clear",
      "close",
      "currentsong",
      "delete",
      "disableoutput",
      "enableoutput",
      "find",
      "idle",
      "kill",
      "list",
      "listallinfo",
      "listplaylistinfo",
      "load",
      "lsinfo",
      "next",
      "notcommands",
      "outputs",
      "password",
      "pause",
      "ping",
      "play",
      "playid",
      "playlistid",
      "playlistinfo",
      "plchanges",
      "previous",
      "save",
      "search",
      "seek",
      "seekid",
      "setvol",
      "stats",
      "status",
      "stop",
      "tagtypes",
      "update"
    };
    // Send the list of supported commands
    const int commandCount = sizeof(supportedCommands) / sizeof(supportedCommands[0]);
    for (int i = 0; i < commandCount; i++) {
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
    mpdClient.print("artists: 0\n");
    mpdClient.print("albums: 0\n");
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
  } else if (command.startsWith("list")) {
    // List command
    if (command.length() > 5) {
      String tagType = command.substring(5);
      tagType.trim();
      if (tagType == "artist") {
        // Return empty list for artist (no local database)
      } else if (tagType == "album") {
        // Return empty list for album (no local database)
      } else if (tagType == "title") {
        // Return playlist titles
        for (int i = 0; i < playlistCountRef; i++) {
          mpdClient.print("Title: " + String(playlistRef[i].name) + "\n");
          mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
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
  } else if (command.startsWith("playid")) {
    // Play ID command
    if (command.length() > 7) {
      int id = command.substring(7).toInt();
      if (id >= 0 && id < playlistCountRef) {
        currentSelectionRef = id;
        startStream(playlistRef[id].url, playlistRef[id].name);
        mpdClient.print(mpdResponseOK());
      } else {
        mpdClient.print(mpdResponseError("No such song"));
      }
    } else if (playlistCountRef > 0 && currentSelectionRef < playlistCountRef) {
      startStream(playlistRef[currentSelectionRef].url, playlistRef[currentSelectionRef].name);
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("No playlist"));
    }
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
    mpdClient.print(mpdResponseError("Not in command list mode"));
  } else if (command.length() == 0) {
    // Empty command
    mpdClient.print(mpdResponseOK());
  } else {
    // Unknown command
    mpdClient.print(mpdResponseError("Unknown command"));
  }
}
