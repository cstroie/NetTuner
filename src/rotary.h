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

#ifndef ROTARY_H
#define ROTARY_H

#include <Arduino.h>

class RotaryEncoder {
private:
  volatile int position = 0;
  int lastCLK = 0;
  volatile unsigned long lastRotaryTime = 0;
  bool buttonPressedFlag = false;

public:
  /**
   * @brief Handle rotary encoder rotation
   * This replaces the previous ISR with a cleaner approach
   */
  void handleRotation();
  
  /**
   * @brief Handle button press
   */
  void handleButtonPress();
  
  /**
   * @brief Get current position
   */
  int getPosition() const;
  
  /**
   * @brief Set position
   */
  void setPosition(int pos);
  
  /**
   * @brief Check if button was pressed
   */
  bool wasButtonPressed();
};

// External reference to rotary encoder instance
extern RotaryEncoder rotaryEncoder;

// Interrupt service routine for rotary encoder
void rotaryISR();

#endif
