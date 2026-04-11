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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"

#include "esp-mixer.h"
#include "tdeck_board.h"
#include "common/system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/i2s_std.h"
#include <string.h>

#define TAG "EspMixerManager"

void EspMixerManager::audioTaskStub(void *param) {
	EspMixerManager *obj = (EspMixerManager *)param;
	obj->audioTask();
}

void EspMixerManager::audioTask() {
	byte *buf = (byte *)heap_caps_calloc(_bufSize, 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	assert(buf);
	int64_t frame_time_us = ((int64_t)(_bufSize / 4) * 1000000ULL) / (int64_t)_freq;
	while (1) {
		int skip = 0;
		if (!_audioSuspended) {
			int64_t t = esp_timer_get_time();
			_mixer->mixCallback(buf, _bufSize);
			t = esp_timer_get_time() - t;
			if (t > frame_time_us) {
				ESP_LOGW(TAG, "Audio frame calc overrun: took %d us to calc %d us worth of audio",
				         (int)t, (int)frame_time_us);
				skip = 1;
			}
		} else {
			memset(buf, 0, _bufSize);
		}
		size_t written = 0;
		if (!skip) {
			i2s_channel_write(_tx_handle, buf, _bufSize, &written, portMAX_DELAY);
		}
	}
}

EspMixerManager::EspMixerManager(int freq, int bufSize)
	: _freq(freq), _bufSize(bufSize) {
}

EspMixerManager::~EspMixerManager() {
}

void EspMixerManager::init() {
	_mixer = new Audio::MixerImpl(_freq);
	assert(_mixer);

	// One TX channel on I2S0 feeding the MAX98357A.
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
	// Double-buffer DMA with enough depth to cover a couple of mix periods.
	chan_cfg.dma_desc_num = 4;
	chan_cfg.dma_frame_num = _bufSize / 4; // stereo 16-bit frames
	if (chan_cfg.dma_frame_num < 240) chan_cfg.dma_frame_num = 240;
	ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &_tx_handle, NULL));

	i2s_std_config_t std_cfg = {};
	std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)_freq);
	std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
	                                                       I2S_SLOT_MODE_STEREO);
	std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
	std_cfg.gpio_cfg.bclk = TDECK_I2S_BCLK_GPIO;
	std_cfg.gpio_cfg.ws   = TDECK_I2S_LRCK_GPIO;
	std_cfg.gpio_cfg.dout = TDECK_I2S_DOUT_GPIO;
	std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
	std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
	std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
	std_cfg.gpio_cfg.invert_flags.ws_inv = false;
	ESP_ERROR_CHECK(i2s_channel_init_std_mode(_tx_handle, &std_cfg));
	ESP_ERROR_CHECK(i2s_channel_enable(_tx_handle));

	// Pin audio to core 1 at priority 7 so it pre-empts the renderer.
	xTaskCreatePinnedToCore(audioTaskStub, "audio", 1024 * 16, (void *)this, 7, NULL, 1);

	_mixer->setReady(true);
}

void EspMixerManager::suspendAudio() {
	_audioSuspended = true;
}

int EspMixerManager::resumeAudio() {
	if (!_audioSuspended) return -2;
	_audioSuspended = false;
	return 0;
}
