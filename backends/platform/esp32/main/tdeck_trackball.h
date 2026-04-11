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

/*
 * T-Deck optical trackball driver.
 *
 * Four GPIO lines pulse as the ball rotates in each cardinal direction;
 * a fifth GPIO is the center button. ISRs count pulses; poll() drains
 * them into an accumulated (dx, dy) and a click edge.
 */

#ifndef BACKENDS_PLATFORM_ESP32_TDECK_TRACKBALL_H
#define BACKENDS_PLATFORM_ESP32_TDECK_TRACKBALL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int dx;
	int dy;
	int8_t click_down;  // 1 = click edge down, -1 = up, 0 = no change
} tdeck_trackball_state_t;

void tdeck_trackball_init(void);
// Drain accumulated motion + click edges since last call. Always writes *out.
void tdeck_trackball_poll(tdeck_trackball_state_t *out);

#ifdef __cplusplus
}
#endif

#endif
