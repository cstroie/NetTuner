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

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "main.h"

class Display {
private:
    Adafruit_SSD1306& displayRef;
    bool displayOn;
    unsigned long lastActivityTime;

public:
    Display(Adafruit_SSD1306& display);
    
    void begin();
    void update(bool isPlaying, const char* streamTitle, const char* streamName, 
                int volume, int bitrate, const String& ipString);
    void clear();
    void showStatus(const String& line1, const String& line2, const String& line3);
    void turnOn();
    void turnOff();
    bool isOn() const;
    void handleTimeout(bool isPlaying, unsigned long currentTime);
    void setActivityTime(unsigned long time);
    unsigned long getLastActivityTime() const;
};

#endif // DISPLAY_H
