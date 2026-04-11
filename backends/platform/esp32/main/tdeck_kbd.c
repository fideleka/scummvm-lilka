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

#include "tdeck_kbd.h"
#include "tdeck_board.h"

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>

#define TAG "tdeck_kbd"

// BBQ10 register map (arturo182 firmware)
#define BBQ10_REG_KEY   0x04   // next FIFO entry (2 bytes: state, key)
#define BBQ10_REG_FIF   0x09   // legacy FIFO alias on some revisions

#define KBD_QUEUE_LEN   32

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static QueueHandle_t s_queue = NULL;

// The T-Deck's BBQ10 keyboard runs Solder Party's i2c_puppet firmware,
// which exposes the next pending key as a single-byte read at the device
// address (no register). Returns 0 if the FIFO is empty, otherwise the
// ASCII byte of the pressed key. We synthesize a press/release pair on
// every non-zero read since the firmware doesn't expose discrete events.
static esp_err_t bbq10_read_key(uint8_t *state, uint8_t *key) {
	uint8_t rx = 0;
	esp_err_t err = i2c_master_receive(s_dev, &rx, 1, pdMS_TO_TICKS(50));
	if (err != ESP_OK) return err;
	if (rx == 0) {
		*state = 0;
		*key = 0;
	} else {
		*state = 1;   // synthetic "pressed" state
		*key = rx;
	}
	return ESP_OK;
}

static void tdeck_kbd_task(void *arg) {
	while (1) {
		uint8_t state = 0, key = 0;
		if (bbq10_read_key(&state, &key) == ESP_OK && state != 0 && key != 0) {
			// The i2c_puppet single-byte protocol gives us only the key
			// code, not press/release edges. Synthesize both a DOWN and
			// an immediate UP so ScummVM sees a complete tap.
			tdeck_kbd_event_t ev = {0};
			ev.pressed = 1;
			ev.raw = key;
			xQueueSend(s_queue, &ev, 0);
			ev.pressed = 0;
			xQueueSend(s_queue, &ev, 0);
			continue;
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

void tdeck_kbd_init(void) {
	if (s_queue) return;
	s_queue = xQueueCreate(KBD_QUEUE_LEN, sizeof(tdeck_kbd_event_t));

	i2c_master_bus_config_t bus_cfg = {
		.i2c_port = TDECK_I2C_PORT,
		.sda_io_num = TDECK_I2C_SDA_GPIO,
		.scl_io_num = TDECK_I2C_SCL_GPIO,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags = { .enable_internal_pullup = true },
	};
	ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

	// Probe the two known BBQ10 addresses (0x55 = arturo182's firmware,
	// 0x1F = older/alternate firmware). The keyboard firmware takes up to
	// ~1 s to boot after the peripheral power rail comes up, so retry for
	// a few seconds before giving up.
	const uint8_t addrs[] = { TDECK_KBD_I2C_ADDR, 0x1F };
	uint8_t picked = 0;
	for (int attempt = 0; attempt < 40 && picked == 0; attempt++) {
		for (size_t i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
			if (i2c_master_probe(s_bus, addrs[i], 100) == ESP_OK) {
				picked = addrs[i];
				break;
			}
		}
		if (picked == 0) vTaskDelay(pdMS_TO_TICKS(100));
	}
	if (picked == 0) {
		ESP_LOGW(TAG, "BBQ10 keyboard not detected on I2C");
		return;
	}

	i2c_device_config_t dev_cfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = picked,
		.scl_speed_hz = TDECK_I2C_CLK_HZ,
	};
	ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));

	xTaskCreatePinnedToCore(tdeck_kbd_task, "tdeck_kbd", 4096, NULL, 5, NULL, 0);
	ESP_LOGI(TAG, "BBQ10 keyboard ready");
}

bool tdeck_kbd_poll(tdeck_kbd_event_t *out) {
	if (!s_queue) return false;
	return xQueueReceive(s_queue, out, 0) == pdTRUE;
}
