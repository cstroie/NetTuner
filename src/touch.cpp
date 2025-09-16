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

TouchButton::TouchButton(uint8_t touchPin, uint16_t touchThreshold, unsigned long debounceMs)
  : pin(touchPin), threshold(touchThreshold), lastState(false),
    pressedFlag(false), debounceTime(debounceMs) {
}

void TouchButton::begin() {
  // Configure touch pad
  touchAttachInterrupt(pin, TouchButton::isr, this);
}

void TouchButton::handle() {
  // With interrupt-based approach, this function is no longer needed
  // Kept for API compatibility
}

bool TouchButton::wasPressed() {
  bool result = pressedFlag;  // Check if press was detected
  if (result) {
    pressedFlag = false;  // Clear flag to prevent reprocessing
  }
  return result;
}

uint16_t TouchButton::getTouchValue() {
  return touchRead(pin);
}

void IRAM_ATTR TouchButton::isr(void* arg) {
  // Cast the argument back to TouchButton instance
  TouchButton* button = static_cast<TouchButton*>(arg);
  if (button) {
    // Set the flag for this specific button instance
    button->pressedFlag = true;
  }
}
