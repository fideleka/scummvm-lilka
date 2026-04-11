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

#include "tdeck_trackball.h"
#include "tdeck_board.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

#include <stdatomic.h>

#define TAG "tdeck_trackball"

// Pulses-per-tick acceleration curve: raw input is small integer
// counts, we scale them into mouse deltas. Values tuned for a typical
// BlackBerry trackball — bigger STEP_PX means the cursor sweeps the
// whole screen with just a few finger-rolls.
#define STEP_PX 8

static atomic_int s_up_cnt;
static atomic_int s_down_cnt;
static atomic_int s_left_cnt;
static atomic_int s_right_cnt;

// Click button is edge-triggered; we keep the last stable level and
// compare on poll.
static volatile int s_last_click_level = 1;   // pulled up when released
static volatile int8_t s_pending_click = 0;   // -1 up, +1 down, 0 none

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR pulse_isr_up(void *arg)    { atomic_fetch_add(&s_up_cnt, 1); }
static void IRAM_ATTR pulse_isr_down(void *arg)  { atomic_fetch_add(&s_down_cnt, 1); }
static void IRAM_ATTR pulse_isr_left(void *arg)  { atomic_fetch_add(&s_left_cnt, 1); }
static void IRAM_ATTR pulse_isr_right(void *arg) { atomic_fetch_add(&s_right_cnt, 1); }

static void IRAM_ATTR click_isr(void *arg) {
	int lvl = gpio_get_level(TDECK_TRACKBALL_CLICK_GPIO);
	if (lvl == s_last_click_level) return;
	s_last_click_level = lvl;
	portENTER_CRITICAL_ISR(&s_mux);
	s_pending_click = (lvl == 0) ? 1 : -1;
	portEXIT_CRITICAL_ISR(&s_mux);
}

static void install_pulse(gpio_num_t pin, void (*isr)(void *)) {
	gpio_config_t cfg = {
		.pin_bit_mask = 1ULL << pin,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_ANYEDGE,
	};
	ESP_ERROR_CHECK(gpio_config(&cfg));
	ESP_ERROR_CHECK(gpio_isr_handler_add(pin, isr, NULL));
}

void tdeck_trackball_init(void) {
	atomic_store(&s_up_cnt, 0);
	atomic_store(&s_down_cnt, 0);
	atomic_store(&s_left_cnt, 0);
	atomic_store(&s_right_cnt, 0);

	esp_err_t err = gpio_install_isr_service(0);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_ERROR_CHECK(err);
	}

	install_pulse(TDECK_TRACKBALL_UP_GPIO,    pulse_isr_up);
	install_pulse(TDECK_TRACKBALL_DOWN_GPIO,  pulse_isr_down);
	install_pulse(TDECK_TRACKBALL_LEFT_GPIO,  pulse_isr_left);
	install_pulse(TDECK_TRACKBALL_RIGHT_GPIO, pulse_isr_right);

	gpio_config_t click_cfg = {
		.pin_bit_mask = 1ULL << TDECK_TRACKBALL_CLICK_GPIO,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_ANYEDGE,
	};
	ESP_ERROR_CHECK(gpio_config(&click_cfg));
	ESP_ERROR_CHECK(gpio_isr_handler_add(TDECK_TRACKBALL_CLICK_GPIO, click_isr, NULL));
	s_last_click_level = gpio_get_level(TDECK_TRACKBALL_CLICK_GPIO);

	ESP_LOGI(TAG, "Trackball ready");
}

static int drain(atomic_int *cnt) {
	int v = atomic_exchange(cnt, 0);
	return v;
}

void tdeck_trackball_poll(tdeck_trackball_state_t *out) {
	int up = drain(&s_up_cnt);
	int dn = drain(&s_down_cnt);
	int lf = drain(&s_left_cnt);
	int rt = drain(&s_right_cnt);

	// Quadratic above 4 pulses for a bit of acceleration.
	int raw_x = rt - lf;
	int raw_y = dn - up;
	int ax = raw_x;
	int ay = raw_y;
	if (raw_x >  4) ax += (raw_x - 4) * 2;
	if (raw_x < -4) ax += (raw_x + 4) * 2;
	if (raw_y >  4) ay += (raw_y - 4) * 2;
	if (raw_y < -4) ay += (raw_y + 4) * 2;
	out->dx = ax * STEP_PX;
	out->dy = ay * STEP_PX;

	portENTER_CRITICAL(&s_mux);
	out->click_down = s_pending_click;
	s_pending_click = 0;
	portEXIT_CRITICAL(&s_mux);
}
