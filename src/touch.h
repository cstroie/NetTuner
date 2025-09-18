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

#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

class TouchButton {
private:
  uint8_t pin;                          // Touch pin number
  uint16_t threshold;                   // Touch threshold value
  bool lastState;                       // Last stable state
  unsigned long lastPressTime;          // Last state change time for debouncing
  volatile bool pressedFlag;            // Flag indicating button press detected
  unsigned long debounceTime;           // Configurable debounce time
  bool useInterrupt;                    // Flag to indicate if interrupt mode is used

public:
  /**
   * @brief Construct a new Touch Button object
   * @param touchPin The touch pin number
   * @param touchThreshold The touch threshold value (default 40)
   * @param debounceMs The debounce time in milliseconds (default 50)
   * @param useInterrupt Whether to use interrupt mode (default false)
   */
  TouchButton(uint8_t touchPin, uint16_t touchThreshold = 40, unsigned long debounceMs = 50, bool useInterrupt = false);

  /**
   * @brief Handle touch button state
   * This function should be called regularly in the main loop
   */
  void handle();

  /**
   * @brief Check if the button was pressed
   * @return true if button was pressed, false otherwise
   */
  bool wasPressed();

  /**
   * @brief Get the current touch value
   * @return Current touch value
   */
  uint16_t getTouchValue();

  /**
   * @brief Interrupt service routine for touch button
   * This function should be called from the ISR
   */
  static void IRAM_ATTR handleInterrupt();
};

// Global pointer for ISR access
extern TouchButton* touchButtonInstance;

#endif // TOUCH_H
