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
  uint8_t pin;
  uint16_t threshold;
  bool lastState;
  unsigned long lastPressTime;
  bool pressedFlag;
  const unsigned long debounceTime = 50;  // 50ms debounce

public:
  /**
   * @brief Construct a new Touch Button object
   * @param touchPin The touch pin number
   * @param touchThreshold The touch threshold value (default 80)
   */
  TouchButton(uint8_t touchPin, uint16_t touchThreshold = 80);

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
};

#endif // TOUCH_H
