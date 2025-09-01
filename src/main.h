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

#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>

// Forward declarations for global variables
extern char streamTitle[];
extern char streamName[];
extern char streamURL[];
extern int bitrate;
extern volatile bool isPlaying;
extern int volume;
extern unsigned long startTime;
extern unsigned long totalPlayTime;
extern unsigned long playStartTime;
extern const char* BUILD_TIME;

// Structure declarations
struct StreamInfo {
  char name[128];
  char url[256];
};

// Forward declarations for global functions
class Audio;
extern Audio* audio;

void stopStream();
void startStream(const char* url, const char* name);
void updateDisplay();
void sendStatusToClients();

#endif // MAIN_H
