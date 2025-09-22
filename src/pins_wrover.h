/*
 * NetTuner - Pin definitions for ESP32-WROVER
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

/*
The PSRAM pins on ESP32-WROVER are:

 • IO6, IO7, IO8, IO9 (shared SPI data pins)
 • IO16 (PSRAM chip select)
 • IO17 (SPI clock)
*/

#ifndef PINS_WROVER_H
#define PINS_WROVER_H

// Default pin definitions for ESP32-WROVER
#define DEFAULT_I2S_DOUT         22  ///< I2S Data Out pin
#define DEFAULT_I2S_BCLK         26  ///< I2S Bit Clock pin
#define DEFAULT_I2S_LRC          25  ///< I2S Left/Right Clock pin
#define DEFAULT_LED_PIN           4  ///< ESP32 internal LED pin
#define DEFAULT_ROTARY_CLK       15  ///< Rotary encoder clock pin
#define DEFAULT_ROTARY_DT        27  ///< Rotary encoder data pin
#define DEFAULT_ROTARY_SW         5  ///< Rotary encoder switch pin
#define DEFAULT_TOUCH_PLAY       12  ///< Touch button play/pause pin
#define DEFAULT_TOUCH_NEXT       -1  ///< Touch button next/volume-up pin
#define DEFAULT_TOUCH_PREV       -1  ///< Touch button previous/volume-down pin
#define DEFAULT_TOUCH_THRESHOLD  40  ///< Touch threshold value
#define DEFAULT_TOUCH_DEBOUNCE  100  ///< Touch debounce time in milliseconds
#define DEFAULT_BOARD_BUTTON      0  ///< ESP32 board button pin (with internal pull-up resistor)
#define DEFAULT_DISPLAY_SDA      13  ///< OLED display SDA pin
#define DEFAULT_DISPLAY_SCL      14  ///< OLED display SCL pin
#define DEFAULT_DISPLAY_TYPE      0  ///< OLED display type (index)
#define DEFAULT_DISPLAY_ADDR   0x3C  ///< OLED display I2C address
#define DEFAULT_DISPLAY_TIMEOUT  30  ///< Display timeout in seconds

#endif // PINS_WROVER_H
