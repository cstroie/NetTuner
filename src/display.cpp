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

#include "display.h"
#include "Spleen6x12.h" 
#include "Spleen8x16.h" 
#include "Spleen16x32.h"
#include <WiFi.h>
#include "main.h"

extern Config config;

Display::Display(Adafruit_SSD1306& display) : displayRef(display), displayOn(true), lastActivityTime(0) {}

void Display::begin() {
    displayRef.begin(SSD1306_SWITCHCAPVCC, config.display_address);
    displayRef.clearDisplay();
    displayRef.setFont(&Spleen8x16);
    displayRef.setTextColor(SSD1306_WHITE);
    displayRef.setCursor(32, 12);
    displayRef.print("NetTuner");
    displayRef.display();
}

void Display::update(bool isPlaying, const char* streamTitle, const char* streamName, 
                     int volume, int bitrate, const String& ipString) {
    // If display is off, don't update
    if (!displayOn) {
        return;
    }
    
    // Get text bounds for display
    int16_t x1, y1;
    uint16_t w, h;
    
    // Clear the display buffer
    displayRef.clearDisplay();
    
    if (isPlaying) {
        // Display when playing
        displayRef.setCursor(0, 12);
        // Fixed '>' character
        displayRef.print(">");
        // Display stream title (first line) with scrolling
        String title = String(streamTitle);
        if (title.length() == 0) {
            title = String(streamName);
        }
        // Scroll title if too long for display (excluding the '>' character)
        // 16 chars fit on a 128px display with '>' and some margin
        // Calculate how many characters we can display (14 chars = 84 pixels)
        int maxDisplayChars = 14;
        if (title.length() > maxDisplayChars) {
            static unsigned long lastTitleScrollTime = 0;
            static int titleScrollOffset = 0;
            static String titleScrollText = "";
            // Reset scroll if text changed
            if (titleScrollText != title) {
                titleScrollText = title;
                titleScrollOffset = 0;
            }
            // Scroll every 500ms
            if (millis() - lastTitleScrollTime > 500) {
                titleScrollOffset++;
                titleScrollOffset++;
                // Reset scroll when we've shown the entire text plus " ~~~ "
                // Calculate based on pixels: each character is 8 pixels wide in Spleen8x16 font
                int totalPixels = (title.length() + 4) * 8;  // +4 for " ~~~ "
                int displayWidth = maxDisplayChars * 8;  // 14 characters * 8 pixels
                if (titleScrollOffset > (totalPixels + displayWidth)) {
                    titleScrollOffset = 0;
                }
                lastTitleScrollTime = millis();
            }
            // Display scrolled text with pixel positioning
            String displayText = title + " ~~~ " + title;
            // Create a temporary string that's long enough to fill the display
            String tempText = displayText + displayText;  // Double it to ensure enough content
            
            // Calculate starting position based on scroll offset
            int startPixel = titleScrollOffset % (displayText.length() * 8);  // 8 pixels per char
            int startChar = startPixel / 8;
            int pixelOffset = startPixel % 8;
            
            // Instead of substring, we'll use pixel positioning
            displayRef.setCursor(16 - pixelOffset, 12);

            // Display text with pixel offset
            String visibleText = tempText.substring(startChar, startChar + maxDisplayChars);
            displayRef.print(visibleText);
        } else {
            // Display title without scrolling
            displayRef.setCursor(16, 12);
            displayRef.print(title);
        }
        // Display stream name (second line)
        displayRef.setCursor(0, 30);
        String stationName = String(streamName);
        // 16 chars fit on a 128px display
        if (stationName.length() > 16) {
            displayRef.print(stationName.substring(0, 16));
        } else {
            displayRef.print(stationName);
        }
        // Display volume and bitrate on third line
        if (config.display_height >= 32) {
            // Display volume and bitrate on third line
            char volStr[20];
            sprintf(volStr, "Vol %2d", volume);
            displayRef.setCursor(0, 45);
            displayRef.print(volStr);
            // Display bitrate on the same line
            if (bitrate > 0) {
                char bitrateStr[20];
                sprintf(bitrateStr, "%3d kbps", bitrate);
                displayRef.getTextBounds(bitrateStr, 0, 45, &x1, &y1, &w, &h);
                displayRef.setCursor(displayRef.width() - w - 1, 45);
                displayRef.print(bitrateStr);
            }
            // Display IP address on the last line, centered
            // Center the IP address
            displayRef.getTextBounds(ipString, 0, 62, &x1, &y1, &w, &h);
            int x = (displayRef.width() - w) / 2;
            if (x < 0) x = 0;
            // Center the IP address
            displayRef.setCursor(x, 62);
            displayRef.print(ipString);
        }
    } else {
        // Display when stopped
        displayRef.setCursor(32, 12);
        displayRef.print("NetTuner");
        // Display current stream name (second line)
        displayRef.setCursor(0, 30);
        if (strlen(streamName) > 0) {
            String currentStream = String(streamName);
            // 16 chars fit on a 128px display
            if (currentStream.length() > 16) {
                displayRef.print(currentStream.substring(0, 16));
            } else {
                displayRef.print(currentStream);
            }
        } else {
            // No stream is currently found in playlist
            displayRef.setCursor(0, 30);
            displayRef.print("No stream");
        }
        // Display volume on third line
        if (config.display_height >= 32) {
            // Display volume and bitrate on third line
            char volStr[20];
            sprintf(volStr, "Vol %2d", volume);
            displayRef.setCursor(0, 45);
            displayRef.print(volStr);
            // Display IP address on the last line, centered
            // Center the IP address
            displayRef.getTextBounds(ipString, 0, 62, &x1, &y1, &w, &h);
            int x = (displayRef.width() - w) / 2;
            if (x < 0) x = 0;
            // Center the IP address
            displayRef.setCursor(x, 62);
            displayRef.print(ipString);
        }
    }
    // Send buffer to display
    displayRef.display();
}

void Display::clear() {
    displayRef.clearDisplay();
    displayRef.display();
}

void Display::showStatus(const String& line1, const String& line2, const String& line3) {
    displayRef.clearDisplay();
    displayRef.setCursor(32, 12);
    displayRef.print("NetTuner");
    
    if (line1.length() > 0) {
        displayRef.setCursor(0, 30);
        displayRef.print(line1);
    }
    
    if (line2.length() > 0) {
        displayRef.setCursor(0, 45);
        displayRef.print(line2);
    }
    
    if (line3.length() > 0) {
        displayRef.setCursor(0, 62);
        displayRef.print(line3);
    }
    
    displayRef.display();
}

void Display::turnOn() {
    displayOn = true;
    displayRef.display();
}

void Display::turnOff() {
    displayOn = false;
    displayRef.clearDisplay();
    displayRef.display();
}

bool Display::isOn() const {
    return displayOn;
}

void Display::handleTimeout(bool isPlaying, unsigned long currentTime) {
    const unsigned long DISPLAY_TIMEOUT = 30000; // 30 seconds
    
    // Handle potential millis() overflow
    if (currentTime < lastActivityTime) {
        lastActivityTime = currentTime; // Reset on overflow
    }
    
    // If we're playing, keep the display on
    if (isPlaying) {
        // Update activity time periodically during playback to prevent timeout
        static unsigned long lastPlaybackActivityUpdate = 0;
        if (currentTime - lastPlaybackActivityUpdate > 5000) { // Every 5 seconds
            lastActivityTime = currentTime;
            lastPlaybackActivityUpdate = currentTime;
        }
        if (!displayOn) {
            displayOn = true;
            displayRef.display(); // Turn display back on
        }
        return;
    }
    
    // If we're not playing, check for timeout
    if (currentTime - lastActivityTime > DISPLAY_TIMEOUT) {
        if (displayOn) {
            displayOn = false;
            displayRef.clearDisplay();
            displayRef.display(); // Update display to clear it
        }
    }
}

void Display::setActivityTime(unsigned long time) {
    lastActivityTime = time;
}

unsigned long Display::getLastActivityTime() const {
    return lastActivityTime;
}
