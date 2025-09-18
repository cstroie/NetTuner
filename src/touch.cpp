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

#include "touch.h"

// Global pointer for ISR access
TouchButton* touchButtonInstance = nullptr;

TouchButton::TouchButton(uint8_t touchPin, uint16_t touchThreshold, unsigned long debounceMs, bool useInterrupt)
  : pin(touchPin), threshold(touchThreshold), lastState(false),
    lastPressTime(0), pressedFlag(false), debounceTime(debounceMs), useInterrupt(useInterrupt) {
  if (useInterrupt) {
    // Set this instance as the global instance for ISR access
    touchButtonInstance = this;
    // Configure touch interrupt to trigger when touch value goes below threshold
    touchAttachInterrupt(pin, handleInterrupt, threshold);
  }
}

void TouchButton::handle() {
  // Read current touch value
  uint16_t touchValue = touchRead(pin);
  // Get current time
  unsigned long currentTime = millis();
  // Check if touch value is below threshold (touched)
  bool currentState = (touchValue < threshold);
  
  // Reset debounce timer when state changes
  if (currentState != lastState) {
    lastPressTime = currentTime;
  }
  
  // Process button state after debounce period has elapsed
  // This ensures stable readings and prevents multiple detections
  if ((currentTime - lastPressTime) > debounceTime) {
    // If button is pressed and we haven't handled this press yet
    if (currentState && !pressedFlag) {
      pressedFlag = true;  // Mark this press as detected
    }
    // If button is released, reset handled flag
    else if (!currentState) {
      pressedFlag = false;
    }
  }
  // Save current state for next iteration
  lastState = currentState;
}

bool TouchButton::wasPressed() {
  // Store current flag state
  bool result = pressedFlag;
  // Clear flag to prevent reprocessing (one-shot detection)
  pressedFlag = false;
  // Return previous flag state
  return result;
}

uint16_t TouchButton::getTouchValue() {
  return touchRead(pin);
}

void IRAM_ATTR TouchButton::handleInterrupt() {
  unsigned long currentTime = millis();
  if (touchButtonInstance) {
    // Implement debouncing in ISR to prevent multiple interrupt triggers
    // Only set pressed flag if enough time has passed since last press
    if ((currentTime - touchButtonInstance->lastPressTime) > touchButtonInstance->debounceTime) {
      touchButtonInstance->pressedFlag = true;
    }
    // Update last press time to current time for debouncing
    touchButtonInstance->lastPressTime = currentTime;
  }
}
