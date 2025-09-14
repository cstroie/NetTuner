/*
 * NetTuner - Pin definitions for ESP32-CAM
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

#ifndef PINS_CAM_H
#define PINS_CAM_H

// Pin definitions for ESP32-CAM
// Note: Some pins are reserved for camera interface
#define DEFAULT_I2S_DOUT         15  ///< I2S Data Out pin
#define DEFAULT_I2S_BCLK          4  ///< I2S Bit Clock pin
#define DEFAULT_I2S_LRC           2  ///< I2S Left/Right Clock pin
#define DEFAULT_LED_PIN          -1  ///< ESP32 internal LED pin
#define DEFAULT_ROTARY_CLK       -1  ///< Rotary encoder clock pin
#define DEFAULT_ROTARY_DT        -1  ///< Rotary encoder data pin
#define DEFAULT_ROTARY_SW        -1  ///< Rotary encoder switch pin
#define DEFAULT_TOUCH_PLAY       12  ///< Touch button play/pause pin
#define DEFAULT_TOUCH_NEXT       13  ///< Touch button next/volume-up pin
#define DEFAULT_TOUCH_PREV       14  ///< Touch button previous/volume-down pin
#define DEFAULT_TOUCH_THRESHOLD  40  ///< Touch threshold value
#define DEFAULT_TOUCH_DEBOUNCE   50  ///< Touch debounce time in milliseconds
#define DEFAULT_BOARD_BUTTON     -1  ///< ESP32 board button pin (with internal pull-up resistor)
#define DEFAULT_DISPLAY_SDA       0  ///< OLED display SDA pin
#define DEFAULT_DISPLAY_SCL      22  ///< OLED display SCL pin
#define DEFAULT_DISPLAY_TYPE      0  ///< OLED display type (index)
#define DEFAULT_DISPLAY_ADDR   0x3C  ///< OLED display I2C address
#define DEFAULT_DISPLAY_TIMEOUT  30  ///< Display timeout in seconds

#endif // PINS_CAM_H
