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
#include "main.h"

// Array to hold pointers to TouchButton instances for ISR access
static TouchButton* touchButtonInstances[TOUCH_PIN_COUNT] = {nullptr};
static int instanceCount = 0;

/**
 * @brief Global interrupt handler that routes to the correct TouchButton instance
 */
void IRAM_ATTR handleTouchInterrupt() {
  // Check all registered instances
  for (int i = 0; i < instanceCount; i++) {
    if (touchButtonInstances[i] != nullptr) {
      touchButtonInstances[i]->handleInterrupt();
    }
  }
}

/**
 * @brief Construct a new Touch Button object
 * 
 * Initializes the touch button with the specified pin and parameters.
 * If interrupt mode is enabled, configures the touch interrupt to trigger
 * when the touch value goes below the threshold.
 * 
 * @param touchPin The touch pin number
 * @param touchThreshold The touch threshold value (default 40)
 * @param debounceMs The debounce time in milliseconds (default 100)
 * @param useInterrupt Whether to use interrupt mode (default false)
 */
TouchButton::TouchButton(uint8_t touchPin, uint16_t touchThreshold, unsigned long debounceMs, bool useInterrupt)
  : pin(touchPin), threshold(touchThreshold), lastState(false),
    lastPressTime(0), pressedFlag(false), debounceTime(debounceMs), useInterrupt(useInterrupt) {
  if (useInterrupt) {
    // Register this instance in the global array
    if (instanceCount < TOUCH_PIN_COUNT) {
      touchButtonInstances[instanceCount] = this;
      instanceCount++;
      // Configure touch interrupt to trigger when touch value goes below threshold
      touchAttachInterrupt(pin, handleTouchInterrupt, threshold);
    }
  }
}

/**
 * @brief Handle touch button state
 * 
 * This function should be called regularly in the main loop to process
 * the touch button state. It implements debouncing logic to prevent 
 * multiple detections from a single physical touch due to electrical 
 * noise or unstable readings. It tracks the state changes and only 
 * registers a press after the state has been stable for the configured 
 * debounce time.
 * 
 * The debouncing algorithm works by:
 * 1. Reading the current touch value
 * 2. Comparing it with the threshold to determine if the button is touched
 * 3. Resetting the debounce timer whenever the state changes
 * 4. Only processing the state after the debounce period has elapsed
 * 5. Setting the pressed flag when a valid press is detected
 */
void TouchButton::handle() {
  // Only process in polling mode, not interrupt mode
  if (!useInterrupt) {
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
}

/**
 * @brief Check if the button was pressed
 * 
 * This method implements a one-shot detection mechanism. Once a press is
 * detected, it returns true only once until the button is released and
 * pressed again. This prevents continuous detection while the button
 * is held down.
 * 
 * @return true if button was pressed, false otherwise
 */
bool TouchButton::wasPressed() {
  // Store current flag state
  bool result = pressedFlag;
  // Clear flag to prevent reprocessing (one-shot detection)
  pressedFlag = false;
  // Return previous flag state
  return result;
}

/**
 * @brief Get the current touch value
 * 
 * This method reads the raw capacitance value from the touch pin.
 * Lower values indicate stronger touch detection.
 * 
 * @return Current touch value
 */
uint16_t TouchButton::getTouchValue() {
  return touchRead(pin);
}

/**
 * @brief Interrupt service routine for touch button
 * 
 * This ISR is triggered when the touch pin value goes below the configured
 * threshold. It implements debouncing and sets the pressed flag for the
 * main code to process.
 * 
 * The ISR implements debouncing by checking if enough time has passed
 * since the last press before setting the pressed flag. This prevents
 * multiple interrupt triggers from a single physical touch.
 * 
 * Note: This function is marked with IRAM_ATTR to ensure it runs from
 * instruction RAM for faster execution, which is critical for ISRs.
 */
void IRAM_ATTR TouchButton::handleInterrupt() {
  unsigned long currentTime = millis();
  // Implement debouncing in ISR to prevent multiple interrupt triggers
  // Only set pressed flag if enough time has passed since last press
  if ((currentTime - lastPressTime) > debounceTime) {
    pressedFlag = true;
  }
  // Update last press time to current time for debouncing
  lastPressTime = currentTime;
}
