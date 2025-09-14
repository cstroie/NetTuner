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

// Forward declarations
class RotaryEncoder;

// Function declarations
void setupRotaryEncoder();
void rotaryISR();

/**
 * @brief Rotary Encoder Handler Class
 * @details This class manages the rotary encoder hardware for volume control
 * and playlist navigation. It handles both rotation detection and button
 * press events with proper debouncing to ensure reliable operation.
 * 
 * The rotary encoder uses a quadrature encoding scheme where two signals
 * (CLK and DT) change state in a specific sequence depending on the rotation
 *  direction.
 */
class RotaryEncoder {
private:
  volatile int position = 0;                 ///< Current rotary encoder position counter
  int lastCLK = 0;                           ///< Last CLK signal state for edge detection
  volatile unsigned long lastRotaryTime = 0; ///< Last rotary event timestamp for debouncing
  bool buttonPressedFlag = false;            ///< Flag indicating button press detected
  bool buttonPressed = false;                ///< Current button press state
  bool lastButtonState = false;              ///< Last button state for edge detection
  unsigned long lastButtonTime = 0;          ///< Last button event timestamp for debouncing

public:
  /**
   * @brief Handle rotary encoder rotation
   * @details Processes rotation events by detecting CLK signal edges and 
   * determining rotation direction based on the DT signal state. Implements 
   * 5ms debouncing to prevent false readings from electrical noise.
   * 
   * The quadrature encoding works as follows:
   * - When rotating clockwise: CLK leads DT
   * - When rotating counter-clockwise: DT leads CLK
   * 
   * Only processes events when CLK transitions from LOW to HIGH to avoid 
   * double-counting.
   */
  void handleRotation();
  
  /**
   * @brief Handle button press (polling method)
   * @details Processes button press events with debouncing to prevent
   * multiple detections from a single press. Sets an internal flag that can
   * be checked and cleared by wasButtonPressed().
   * 
   * The button is connected with a pull-up resistor, so a press is detected
   * when the signal transitions from HIGH to LOW.
   */
  void handleButtonPress();
  
  /**
   * @brief Get current position
   * @details Returns the current rotary encoder position counter value.
   * The position increases with clockwise rotation and decreases with
   * counter-clockwise rotation.
   * @return Current position value
   */
  int getPosition() const volatile;
  
  /**
   * @brief Set position
   * @details Sets the rotary encoder position counter to a specific value.
   * This can be used to reset the position or synchronize with external state.
   * @param pos New position value
   */
  void setPosition(int pos);
  
  /**
   * @brief Check if button was pressed
   * @details Returns the button press status and automatically clears the flag.
   * This ensures that each button press is only processed once.
   * @return true if button was pressed since last check, false otherwise
   */
  bool wasButtonPressed();
};

#endif
