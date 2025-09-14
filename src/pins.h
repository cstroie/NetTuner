/*
 * NetTuner - Pin definitions
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

#ifndef PINS_H
#define PINS_H

// Default pin definitions with ifndef guards to prevent overwriting
#ifndef DEFAULT_I2S_DOUT
#define DEFAULT_I2S_DOUT         26  ///< I2S Data Out pin
#endif

#ifndef DEFAULT_I2S_BCLK
#define DEFAULT_I2S_BCLK         27  ///< I2S Bit Clock pin
#endif

#ifndef DEFAULT_I2S_LRC
#define DEFAULT_I2S_LRC          25  ///< I2S Left/Right Clock pin
#endif

#ifndef DEFAULT_LED_PIN
#define DEFAULT_LED_PIN           2  ///< ESP32 internal LED pin
#endif

#ifndef DEFAULT_ROTARY_CLK
#define DEFAULT_ROTARY_CLK       18  ///< Rotary encoder clock pin
#endif

#ifndef DEFAULT_ROTARY_DT
#define DEFAULT_ROTARY_DT        19  ///< Rotary encoder data pin
#endif

#ifndef DEFAULT_ROTARY_SW
#define DEFAULT_ROTARY_SW        23  ///< Rotary encoder switch pin
#endif

#ifndef DEFAULT_TOUCH_PLAY
#define DEFAULT_TOUCH_PLAY       12  ///< Touch button play/pause pin
#endif

#ifndef DEFAULT_TOUCH_NEXT
#define DEFAULT_TOUCH_NEXT       13  ///< Touch button next/volume-up pin
#endif

#ifndef DEFAULT_TOUCH_PREV
#define DEFAULT_TOUCH_PREV       14  ///< Touch button previous/volume-down pin
#endif

#ifndef DEFAULT_TOUCH_THRESHOLD
#define DEFAULT_TOUCH_THRESHOLD  40  ///< Touch threshold value
#endif

#ifndef DEFAULT_TOUCH_DEBOUNCE
#define DEFAULT_TOUCH_DEBOUNCE   50  ///< Touch debounce time in milliseconds
#endif

#ifndef DEFAULT_BOARD_BUTTON
#define DEFAULT_BOARD_BUTTON      0  ///< ESP32 board button pin (with internal pull-up resistor)
#endif

#ifndef DEFAULT_DISPLAY_SDA
#define DEFAULT_DISPLAY_SDA      21  ///< OLED display SDA pin
#endif

#ifndef DEFAULT_DISPLAY_SCL
#define DEFAULT_DISPLAY_SCL      22  ///< OLED display SCL pin
#endif

#ifndef DEFAULT_DISPLAY_TYPE
#define DEFAULT_DISPLAY_TYPE      0  ///< Default display type index
#endif

#ifndef DEFAULT_DISPLAY_ADDR
#define DEFAULT_DISPLAY_ADDR   0x3C  ///< OLED display I2C address
#endif

#ifndef DEFAULT_DISPLAY_TIMEOUT
#define DEFAULT_DISPLAY_TIMEOUT  30  ///< OLED display timeout in seconds
#endif

#endif // PINS_H
