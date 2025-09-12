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

int yUpdate[OLED_COUNT][4] = {
    {12, -1, 28, -1},     // 128x32
    {12, 30, 45, 62},     // 128x64 
    {12, -1, 22, 31}      // 128x32 small font
};

int yStatus[OLED_COUNT][4] = {
    {12, -1, 28, -1},     // 128x32
    {12, 30, 45, 62},     // 128x64 
    {12, -1, 22, 31}      // 128x32 small font
};


/**
 * @brief Construct a new Display object
 * 
 * Initializes the display with a reference to an Adafruit_SSD1306 instance,
 * sets the display to on state, and initializes the activity time to 0.
 * 
 * @param display Reference to Adafruit_SSD1306 display instance
 */
Display::Display(Adafruit_SSD1306& display) : displayRef(display), displayOn(true), lastActivityTime(0) {}

/**
 * @brief Initialize the display
 * 
 * Configures and initializes the OLED display with default settings,
 * including font, text color, and initial "NetTuner" splash screen.
 * This method must be called before any other display operations.
 */
void Display::begin() {
    displayRef.begin(SSD1306_SWITCHCAPVCC, config.display_address);
    if (config.display_height == 64) {
        displayType = OLED_128x64;
    }
    else {
        displayType = OLED_128x32s;
    }
    showLogo();
}

/**
 * @brief Clear the display
 * 
 * Clears the display buffer and updates the physical display.
 * This method immediately clears the screen regardless of display state.
 */
void Display::clear() {
    displayRef.clearDisplay();
    displayRef.display();
}

void Display::setFontCursor(int x, int y, char c = 'l') {
    static int lastY = config.display_height;
    // New screen
    if (y < lastY) { lastY = 0; }
    // Get the available space
    int h = y - lastY;
    // Choose the adequate font
    if      (h >= 28) { displayRef.setFont(&Spleen16x32); }
    else if (h >= 12) { displayRef.setFont(&Spleen8x16);  }
    else if (h >= 8)  { displayRef.setFont(&Spleen6x12);  }
    else              { displayRef.setFont(&Spleen6x12);  }
    // Set the cursor position
    displayRef.setCursor(x, y);
    // Keep the last Y
    lastY = y;
}

void Display::showLogo() {
    // Clear the buffer
    displayRef.clearDisplay();
    displayRef.setTextColor(SSD1306_WHITE);
    setFontCursor(0, yStatus[displayType][2]);
    displayRef.print("NetTuner");
    // Show the buffer
    displayRef.display();
}

/**
 * @brief Update display with current playback information
 * 
 * Updates the display with current playback status, stream information,
 * volume level, and IP address. Implements scrolling text for long titles.
 * 
 * Scrolling Text Implementation:
 * - For titles longer than 14 characters, implements smooth scrolling
 * - Uses a static offset that increments every 500ms
 * - Displays "title ~~~ title" to create seamless scrolling effect
 * - Calculates pixel positioning for smooth character-by-character scroll
 * 
 * @param isPlaying Current playback state
 * @param streamTitle Current stream title
 * @param streamName Current stream name
 * @param volume Current volume level (0-22)
 * @param bitrate Current stream bitrate
 * @param ipString IP address to display
 */
void Display::update(bool isPlaying, const char* streamTitle, const char* streamName, 
                     int volume, int bitrate, const String& ipString) {
    // If display is off, don't update to save power
    if (!displayOn) {
        return;
    }
    
    // Text bounds variables for centering calculations
    int16_t x1, y1;
    uint16_t w, h;
    
    // Clear the display buffer for fresh content
    displayRef.clearDisplay();
    displayRef.setTextColor(SSD1306_WHITE);
    
    if (isPlaying) {
        // Display when playing - show stream information with scrolling title
        setFontCursor(0, yUpdate[displayType][0]);
        // Fixed '>' character to indicate playing state
        displayRef.print(">");
        
        // Display stream title (first line) with scrolling for long titles
        String title = streamName;
        if (displayType == OLED_128x64) {
            if (String(streamTitle).length() > 0) {
                title = String(streamTitle);
            }
        } else {
            if (String(streamTitle).length() > 0) {
                title = title + ": " + String(streamTitle);
            }
        }
        
        // Scroll title if too long for display (excluding the '>' character)
        // 16 chars fit on a 128px display with '>' and some margin
        // Calculate how many characters we can display (14 chars = 84 pixels)
        int maxDisplayChars = 14;
        if (title.length() > maxDisplayChars) {
            // Static variables for scrolling state management
            static unsigned long lastTitleScrollTime = 0;
            static int titleScrollOffset = 0;
            static String titleScrollText = "";
            
            // Reset scroll if text changed
            if (titleScrollText != title) {
                titleScrollText = title;
                titleScrollOffset = 0;
            }
            
            // Scroll every 500ms for smooth animation
            if (millis() - lastTitleScrollTime > 500) {
                titleScrollOffset++;
                // Reset scroll when we've shown the entire text plus " ~~~ "
                if (titleScrollOffset > (int)(title.length() + 4)) {  // +4 for " ~~~ "
                    titleScrollOffset = 0;
                }
                lastTitleScrollTime = millis();
            }
            
            // Display scrolled text
            String displayText = title + " ~~~ " + title;
            // Create a temporary string that's long enough to fill the display
            String tempText = displayText + displayText;  // Double it to ensure enough content
            setFontCursor(16, yUpdate[displayType][0]);
            displayRef.print(tempText.substring(titleScrollOffset, titleScrollOffset + maxDisplayChars));
        } else {
            // Display title without scrolling for short titles
            setFontCursor(0, yUpdate[displayType][0]);
            displayRef.print(title);
        }
        
        if (yUpdate[displayType][1] > 0) {
            // Display stream name (second line) - truncated if too long
            setFontCursor(0, yUpdate[displayType][1]);
            String stationName = String(streamName);
            // 16 chars fit on a 128px display
            if (stationName.length() > 16) {
                displayRef.print(stationName.substring(0, 16));
            } else {
                displayRef.print(stationName);
            }
        }
        
        if (yUpdate[displayType][2] > 0) {
            // Display volume and bitrate on third line
            char volStr[20];
            sprintf(volStr, "Vol %2d", volume);
            setFontCursor(0, yUpdate[displayType][2]);
            displayRef.print(volStr);
            
            // Display bitrate on the same line if available
            if (bitrate > 0) {
                char bitrateStr[20];
                sprintf(bitrateStr, "%3d kbps", bitrate);
                displayRef.getTextBounds(bitrateStr, 0, yUpdate[displayType][2], &x1, &y1, &w, &h);
                setFontCursor(displayRef.width() - w - 1, yUpdate[displayType][2]);
                displayRef.print(bitrateStr);
            }
        }
            
        if (yUpdate[displayType][3] > 0) {
            // Display IP address on the last line, centered
            displayRef.getTextBounds(ipString, 0, yUpdate[displayType][3], &x1, &y1, &w, &h);
            int x = (displayRef.width() - w) / 2;
            if (x < 0) x = 0;
            // Center the IP address
            setFontCursor(x, yUpdate[displayType][3]);
            displayRef.print(ipString);
        }
    } else {
        // Display when stopped
        int lineStream = 0;
        if (yUpdate[displayType][1] > 0) {
            setFontCursor(32, yUpdate[displayType][0]);
            displayRef.print("NetTuner");
            lineStream = 1;
        }
            
        // Display current stream name (second line) or "No stream" if none selected
        setFontCursor(0, yUpdate[displayType][lineStream]);
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
            setFontCursor(0, yUpdate[displayType][lineStream]);
            displayRef.print("No stream");
        }
        
        // Display volume on third line (only for displays with sufficient height)
        if (yUpdate[displayType][2] > 0) {
            // Display volume level
            char volStr[20];
            sprintf(volStr, "Vol %2d", volume);
            setFontCursor(0, yUpdate[displayType][2]);
            displayRef.print(volStr);
        }

        if (yUpdate[displayType][3] > 0) {
            // Display IP address on the last line, centered
            displayRef.getTextBounds(ipString, 0, yUpdate[displayType][3], &x1, &y1, &w, &h);
            int x = (displayRef.width() - w) / 2;
            if (x < 0) x = 0;
            // Center the IP address
            setFontCursor(x, yUpdate[displayType][3]);
            displayRef.print(ipString);
        }
    }
    
    // Send buffer to display to make changes visible
    displayRef.display();
}

/**
 * @brief Show status information on display
 * 
 * Displays a standardized status screen with "NetTuner" title and
 * up to three lines of additional information.
 * 
 * @param line1 First line of information (displayed at y=30)
 * @param line2 Second line of information (displayed at y=45)
 * @param line3 Third line of information (displayed at y=62)
 */
void Display::showStatus(const String& line1, const String& line2, const String& line3) {
    // Clear the buffer
    displayRef.clearDisplay();
    displayRef.setTextColor(SSD1306_WHITE);
    // Different modes for different display sizes
    if (displayType == OLED_128x64) {
        setFontCursor(32, yStatus[displayType][0]);
        displayRef.print("NetTuner");
        // Display each line if it contains content
        if (line1.length() > 0) {
            setFontCursor(0, yStatus[displayType][1]);
            displayRef.print(line1);
        }
        if (line2.length() > 0) {
            setFontCursor(0, yStatus[displayType][2]);
            displayRef.print(line2);
        }
        if (line3.length() > 0) {
            setFontCursor(0, yStatus[displayType][3]);
            displayRef.print(line3);
        }
    } else {
        // Display first line
        if (line1.length() > 0) {
            setFontCursor(0, yStatus[displayType][0]);
            displayRef.print(line1);
        }
        // Displey the second or third line if they have contemt
        if (line2.length() > 0) {
            setFontCursor(0, yStatus[displayType][2]);
            displayRef.print(line2);
        } else if (line3.length() > 0) {
            setFontCursor(0, yStatus[displayType][2]);
            displayRef.print(line3);
        }
    }
    // Show the buffer
    displayRef.display();
}

/**
 * @brief Turn display on
 * 
 * Enables display output and refreshes the screen.
 * This method immediately turns the display on and shows current content.
 */
void Display::turnOn() {
    displayOn = true;
    displayRef.display();
}

/**
 * @brief Turn display off
 * 
 * Disables display output and clears the screen.
 * This method immediately turns the display off to save power.
 */
void Display::turnOff() {
    displayOn = false;
    displayRef.clearDisplay();
    displayRef.display();
}

/**
 * @brief Check if display is on
 * 
 * @return true if display is currently on
 * @return false if display is currently off
 */
bool Display::isOn() const {
    return displayOn;
}

/**
 * @brief Handle display timeout
 * 
 * Manages automatic display power management based on playback state
 * and user activity. Turns display off after 30 seconds of inactivity
 * when not playing, and keeps it on during playback.
 * 
 * Timeout Logic:
 * - When playing: Display stays on, activity time updated every 5 seconds
 * - When stopped: Display turns off after 30 seconds of inactivity
 * - Handles millis() overflow by resetting activity time
 * 
 * @param isPlaying Current playback state
 * @param currentTime Current system time in milliseconds
 */
void Display::handleTimeout(bool isPlaying, unsigned long currentTime) {
    const unsigned long DISPLAY_TIMEOUT = 30000; // 30 seconds
    
    // Handle potential millis() overflow by resetting activity time
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

/**
 * @brief Set last activity time
 * 
 * Updates the timestamp of the last user activity, used for timeout
 * management.
 * 
 * @param time Timestamp in milliseconds
 */
void Display::setActivityTime(unsigned long time) {
    lastActivityTime = time;
}

/**
 * @brief Get last activity time
 * 
 * @return unsigned long Timestamp of last user activity
 */
unsigned long Display::getLastActivityTime() const {
    return lastActivityTime;
}
