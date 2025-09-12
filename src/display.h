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


/**
 * @brief Display types supported by the system
 * 
 * Enumeration of all supported OLED display types with their dimensions
 */
enum display_t {OLED_128x64, OLED_128x32, OLED_128x32s, OLED_COUNT};

/**
 * @brief Array of display type names
 * 
 * Human-readable names for each display type, indexed by display_t values
 */
extern char* displayNames[OLED_COUNT];

/**
 * @brief Array of display sizes
 * 
 * Width and height for each display type, indexed by display_t values
 * Format: [display_type][0=width, 1=height]
 */
extern int displaySizes[OLED_COUNT][2];


/**
 * @brief Y-coordinate arrays for update display
 * 
 * Predefined Y-coordinates for different display types and lines
 * Format: [display_type][line_number] where line_number 0-3
 */
extern int yUpdate[OLED_COUNT][4];

/**
 * @brief Y-coordinate arrays for status display
 * 
 * Predefined Y-coordinates for different display types and lines in status mode
 * Format: [display_type][line_number] where line_number 0-3
 */
extern int yStatus[OLED_COUNT][4];

/**
 * @brief Get the number of configured display types
 * 
 * @return int Number of display types available in the system
 */
int getDisplayTypeCount();

/**
 * @brief Get the name of a display type by index
 * 
 * @param index Display type index (0 to OLED_COUNT-1)
 * @return const char* Name of the display type, or nullptr if index is invalid
 */
const char* getDisplayTypeName(int index);

/**
 * @brief Get the size of a display type by index
 * 
 * @param index Display type index (0 to OLED_COUNT-1)
 * @param width Pointer to store display width
 * @param height Pointer to store display height
 * @return true if successful, false if index is invalid
 */
bool getDisplaySize(int index, int* width, int* height);


/**
 * @brief Display management class for OLED screen
 * 
 * This class encapsulates all display functionality for the NetTuner,
 * including initialization, updating with playback information, status
 * display, and power management.
 */
class Display {
private:
    Adafruit_SSD1306& displayRef;    ///< Reference to the SSD1306 display object
    bool displayOn;                  ///< Flag indicating if display is currently on
    unsigned long lastActivityTime;  ///< Timestamp of last user activity
    enum display_t displayType;

public:
    /**
     * @brief Construct a new Display object
     * 
     * @param display Reference to Adafruit_SSD1306 display instance
     * @param display_type Type of display being used
     */
    Display(Adafruit_SSD1306& display, enum display_t display_type);
    
    /**
     * @brief Initialize the display
     * 
     * Configures and initializes the OLED display with default settings,
     * including font, text color, and initial "NetTuner" splash screen.
     */
    void begin();
    
    void printAt(const char* text, int x, int y, char align);

    void printAt(const String text, int x, int y, char align);
    
    /**
     * @brief Update display with current playback information
     * 
     * Updates the display with current playback status, stream information,
     * volume level, and IP address. Implements scrolling text for long titles.
     * 
     * @param isPlaying Current playback state
     * @param streamTitle Current stream title
     * @param streamName Current stream name
     * @param volume Current volume level (0-22)
     * @param bitrate Current stream bitrate
     * @param ipString IP address to display
     */
    void update(bool isPlaying, const char* streamTitle, const char* streamName, 
                int volume, int bitrate, const String& ipString);
    
    /**
     * @brief Clear the display
     * 
     * Clears the display buffer and updates the physical display.
     */
    void clear();
    
    void showLogo();
    
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
    void showStatus(const String& line1, const String& line2, const String& line3);
    
    /**
     * @brief Turn display on
     * 
     * Enables display output and refreshes the screen.
     */
    void turnOn();
    
    /**
     * @brief Turn display off
     * 
     * Disables display output and clears the screen.
     */
    void turnOff();
    
    /**
     * @brief Check if display is on
     * 
     * @return true if display is currently on
     * @return false if display is currently off
     */
    bool isOn() const;
    
    /**
     * @brief Handle display timeout
     * 
     * Manages automatic display power management based on playback state
     * and user activity. Turns display off after 30 seconds of inactivity
     * when not playing, and keeps it on during playback.
     * 
     * @param isPlaying Current playback state
     * @param currentTime Current system time in milliseconds
     */
    void handleTimeout(bool isPlaying, unsigned long currentTime);
    
    /**
     * @brief Set last activity time
     * 
     * Updates the timestamp of the last user activity, used for timeout
     * management.
     * 
     * @param time Timestamp in milliseconds
     */
    void setActivityTime(unsigned long time);
    
    /**
     * @brief Get last activity time
     * 
     * @return unsigned long Timestamp of last user activity
     */
    unsigned long getLastActivityTime() const;
};

#endif // DISPLAY_H
