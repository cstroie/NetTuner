#include "rotary.h"
#include "main.h"

// Global rotary encoder instance
RotaryEncoder rotaryEncoder;

void RotaryEncoder::handleRotation() {
  unsigned long currentTime = millis();
  
  // Debounce rotary encoder (ignore if less than 5ms since last event)
  if (currentTime - lastRotaryTime < 5) {
    return;
  }
  
  int CLK = digitalRead(config.rotary_clk);  // Read clock signal
  int DT = digitalRead(config.rotary_dt);    // Read data signal
  
  // Only process when CLK transitions from LOW to HIGH
  if (CLK == HIGH && lastCLK == LOW) {
    // Determine rotation direction based on DT state
    if (DT == LOW) {
      position++;      // Clockwise rotation
    } else {
      position--;      // Counter-clockwise rotation
    }
    lastRotaryTime = currentTime;  // Update last event time
  }
  lastCLK = CLK;
}

void RotaryEncoder::handleButtonPress() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  // Debounce the button press (ignore if less than 50ms since last press)
  if (interruptTime - lastInterruptTime > 50) {
    buttonPressedFlag = true;
  }
  lastInterruptTime = interruptTime;
}

int RotaryEncoder::getPosition() const {
  return position;
}

void RotaryEncoder::setPosition(int pos) {
  position = pos;
}

bool RotaryEncoder::wasButtonPressed() {
  bool result = buttonPressedFlag;
  buttonPressedFlag = false;
  return result;
}

// Interrupt service routine for rotary encoder
void rotaryISR() {
  rotaryEncoder.handleRotation();
}
