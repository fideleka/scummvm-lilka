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
 * T-Deck BlackBerry Q10 I2C keyboard driver.
 *
 * The T-Deck exposes a BBQ10 keyboard controller (arturo182's firmware)
 * at I2C address 0x55. The controller keeps a FIFO of 2-byte entries
 * (state, key). State: 1 = pressed, 2 = held, 3 = released.
 *
 * This driver starts a FreeRTOS task that drains the FIFO and pushes
 * ASCII bytes into a bounded queue. pollEvent() reads the queue from the
 * ScummVM main task.
 */

#ifndef BACKENDS_PLATFORM_ESP32_TDECK_KBD_H
#define BACKENDS_PLATFORM_ESP32_TDECK_KBD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint8_t pressed;   // 1 = key down, 0 = key up
	uint8_t raw;       // raw BBQ10 keycode (ASCII for most keys)
} tdeck_kbd_event_t;

void tdeck_kbd_init(void);
// Returns true if an event was available and written to *out.
bool tdeck_kbd_poll(tdeck_kbd_event_t *out);

#ifdef __cplusplus
}
#endif

#endif
