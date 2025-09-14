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

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <Arduino.h>

// Define MAX_PLAYLIST_SIZE before using it
#define MAX_PLAYLIST_SIZE 20

// Structure for playlist items
struct StreamInfo {
  char name[96];
  char url[128];
};

class Playlist {
private:
  StreamInfo playlist[MAX_PLAYLIST_SIZE];
  int count;
  int current;
  
public:
  // Constructor
  Playlist();
  
  // Playlist management methods
  void load();
  void save();
  void setItem(int index, const char* name, const char* url);
  void addItem(const char* name, const char* url);
  void removeItem(int index);
  void clear();
  
  // Getters
  int getCount() const { return count; }
  int getCurrent() const { return current; }
  const StreamInfo& getItem(int index) const { return playlist[index]; }

  // Setters
  void setCurrent(int index) { current = index; }
  
  // Utility methods
  void validate();
};

#endif // PLAYLIST_H
