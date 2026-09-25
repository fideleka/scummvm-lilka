/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* Lilka v2 pin map. Names retain the inherited backend's TDECK prefix
 * until the hardware backend is fully separated. Source: Lilka SDK config.h. */

#ifndef BACKENDS_PLATFORM_ESP32_TDECK_BOARD_H
#define BACKENDS_PLATFORM_ESP32_TDECK_BOARD_H

#include "driver/gpio.h"
#include "driver/spi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Shared SPI bus (LCD + SD) ------------------------------------- */
#define TDECK_SPI_HOST              SPI2_HOST
#define TDECK_SPI_SCK_GPIO          GPIO_NUM_18
#define TDECK_SPI_MOSI_GPIO         GPIO_NUM_17
#define TDECK_SPI_MISO_GPIO         GPIO_NUM_8

/* ----- ST7789 LCD (280x240 landscape, SPI) --------------------------- */
#define TDECK_LCD_H_RES             280
#define TDECK_LCD_V_RES             240
#define TDECK_LCD_BITS_PER_PIXEL    16
#define TDECK_LCD_CS_GPIO           GPIO_NUM_7
#define TDECK_LCD_DC_GPIO           GPIO_NUM_15
#define TDECK_LCD_RST_GPIO          GPIO_NUM_NC
#define TDECK_LCD_BL_GPIO           GPIO_NUM_NC
#define TDECK_LCD_PIXEL_CLOCK_HZ    (40 * 1000 * 1000)
#define TDECK_LCD_SPI_MODE          0
#define TDECK_LCD_CMD_BITS          8
#define TDECK_LCD_PARAM_BITS        8

/* ----- MAX98357A (I2S) speaker amp ----------------------------------- */
#define TDECK_I2S_BCLK_GPIO         GPIO_NUM_42
#define TDECK_I2S_LRCK_GPIO         GPIO_NUM_1
#define TDECK_I2S_DOUT_GPIO         GPIO_NUM_2

/* ----- I2C bus (BBQ10 keyboard + touchpad) --------------------------- */
#define TDECK_I2C_PORT              0
#define TDECK_I2C_SCL_GPIO          GPIO_NUM_8
#define TDECK_I2C_SDA_GPIO          GPIO_NUM_18
#define TDECK_I2C_CLK_HZ            100000
#define TDECK_KBD_I2C_ADDR          0x55
#define TDECK_KBD_INT_GPIO          GPIO_NUM_46

/* ----- Trackball (four direction pulse lines + center click) --------- */
#define TDECK_TRACKBALL_UP_GPIO     GPIO_NUM_3
#define TDECK_TRACKBALL_DOWN_GPIO   GPIO_NUM_15
#define TDECK_TRACKBALL_LEFT_GPIO   GPIO_NUM_1
#define TDECK_TRACKBALL_RIGHT_GPIO  GPIO_NUM_2
#define TDECK_TRACKBALL_CLICK_GPIO  GPIO_NUM_0

/* ----- microSD (SPI mode, shares SPI host with LCD) ------------------ */
#define TDECK_SD_CS_GPIO            GPIO_NUM_16

/* Turn on the peripheral power rail and set up the shared SPI bus.
 * Must be called exactly once, before any of the LCD / SD / trackpad
 * drivers touch the bus. Idempotent on repeated calls from the same task. */
void tdeck_board_init(void);

/* Serialize the LCD's queued DMA transfers with the SD card's polling SPI
 * transfers. Hold the lock until all queued LCD callbacks have completed. */
void tdeck_spi_lock(void);
void tdeck_spi_unlock(void);

#ifdef __cplusplus
}
#endif

#endif
