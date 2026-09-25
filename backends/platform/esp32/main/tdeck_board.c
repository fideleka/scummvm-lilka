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

#include "tdeck_board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <assert.h>

#define TAG "tdeck_board"

static bool s_inited = false;
static SemaphoreHandle_t s_spi_mutex;

void tdeck_spi_lock(void) {
	assert(s_spi_mutex);
	xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
}

void tdeck_spi_unlock(void) {
	assert(s_spi_mutex);
	xSemaphoreGive(s_spi_mutex);
}

void tdeck_board_init(void) {
	if (s_inited) return;
	s_inited = true;

	// Pre-drive ALL SPI chip-select pins high so that adding one device
	// (e.g. the SD card) doesn't accidentally chatter the LCD controller
	// because its CS is still floating. The real owners of these pins
	// (esp_lcd for LCD CS, sdspi_host for SD CS) will reconfigure them
	// when they initialize.
	const gpio_num_t cs_pins[] = {
		TDECK_LCD_CS_GPIO,
		TDECK_SD_CS_GPIO,
	};
	for (size_t i = 0; i < sizeof(cs_pins) / sizeof(cs_pins[0]); i++) {
		gpio_config_t cs_cfg = {
			.pin_bit_mask = 1ULL << cs_pins[i],
			.mode = GPIO_MODE_OUTPUT,
			.pull_up_en = GPIO_PULLUP_ENABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type = GPIO_INTR_DISABLE,
		};
		ESP_ERROR_CHECK(gpio_config(&cs_cfg));
		gpio_set_level(cs_pins[i], 1);
	}

	// Initialize the shared SPI bus used by the LCD and the SD card.
	// Max transfer size is one full RGB565 scanline row for the LCD.
	spi_bus_config_t buscfg = {
		.sclk_io_num = TDECK_SPI_SCK_GPIO,
		.mosi_io_num = TDECK_SPI_MOSI_GPIO,
		.miso_io_num = TDECK_SPI_MISO_GPIO,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = TDECK_LCD_H_RES * TDECK_LCD_V_RES * sizeof(uint16_t) + 8,
	};
	ESP_ERROR_CHECK(spi_bus_initialize(TDECK_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
	s_spi_mutex = xSemaphoreCreateMutex();
	assert(s_spi_mutex);

	ESP_LOGI(TAG, "T-Deck board init done");
}
