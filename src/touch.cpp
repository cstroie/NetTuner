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
  touchAttachInterrupt(pin, isrWrapper, threshold);
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

void IRAM_ATTR TouchButton::isr() {
  // Simple interrupt handler - just set the flag
  // The actual state handling will be done in the main loop
  pressedFlag = true;
}

void IRAM_ATTR TouchButton::isrWrapper(void* arg) {
  // Static cast to TouchButton pointer and call the ISR
  // Note: This assumes the touch buttons are created as global objects
  // In a more robust implementation, we would need to maintain a registry
  // of touch buttons and their pins to properly route interrupts
}
