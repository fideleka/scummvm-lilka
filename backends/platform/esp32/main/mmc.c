// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "esp_littlefs.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "blkcache.h"
#include "ff.h"
#include "diskio_impl.h"
#include "tdeck_board.h"

static const char *TAG = "tdeck_sd";

static sdmmc_card_t card;
static blkcache_handle_t *bc;
FATFS *fatfs;

static DSTATUS dio_init(unsigned char pdrv)   { return 0; }
static DSTATUS dio_status(unsigned char pdrv) { return 0; }

static DRESULT dio_read(unsigned char pdrv, unsigned char *buff, uint32_t sector, unsigned count) {
	blkcache_read_sectors(bc, buff, sector, count);
	return RES_OK;
}

static DRESULT dio_write(unsigned char pdrv, const unsigned char *buff, uint32_t sector, unsigned count) {
	blkcache_write_sectors(bc, buff, sector, count);
	return RES_OK;
}

static DRESULT dio_ioctl(unsigned char pdrv, unsigned char cmd, void *buff) {
	if (cmd == CTRL_SYNC) {
		// nothing to do
	} else if (cmd == GET_SECTOR_COUNT) {
		*((DWORD *)buff) = card.csd.capacity;
	} else if (cmd == GET_SECTOR_SIZE) {
		*((DWORD *)buff) = card.csd.sector_size;
	} else if (cmd == GET_BLOCK_SIZE) {
		return RES_ERROR;
	} else if (cmd == CTRL_TRIM) {
		return RES_ERROR;
	} else {
		return RES_ERROR;
	}
	return RES_OK;
}

void sdcard_mount_blkcache(const char *mountpoint, int files) {
	// The shared SPI bus must already be initialized by tdeck_board_init().
	tdeck_board_init();

	// Mount the baked-in LittleFS partition first, at /sdcard. This is
	// always present so ScummVM can find the bundled MI1 EGA demo and
	// the support files we shipped with the firmware. If a real SD card
	// is also inserted, it will be exposed at /sd (see below) so its
	// contents are still reachable but don't shadow the bundled data.
	{
		esp_vfs_littlefs_conf_t lfs_cfg = {
			.base_path = mountpoint,
			.partition_label = "storage",
			.format_if_mount_failed = false,
			.dont_mount = false,
		};
		esp_err_t le = esp_vfs_littlefs_register(&lfs_cfg);
		if (le != ESP_OK) {
			ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(le));
		} else {
			size_t total = 0, used = 0;
			if (esp_littlefs_info("storage", &total, &used) == ESP_OK) {
				ESP_LOGI(TAG, "LittleFS mounted at %s: %u/%u bytes used",
				         mountpoint, (unsigned)used, (unsigned)total);
			}
		}
	}

	// Now ALSO try to mount the physical SD card at /sd (a different
	// mount point). If it fails, no harm done — the bundled LittleFS
	// game still works.
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.slot = TDECK_SPI_HOST;
	host.max_freq_khz = SDMMC_FREQ_PROBING;

	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = TDECK_SD_CS_GPIO;
	slot_config.host_id = TDECK_SPI_HOST;

	sdspi_dev_handle_t sdspi_handle;
	ESP_ERROR_CHECK(sdspi_host_init());
	ESP_ERROR_CHECK(sdspi_host_init_device(&slot_config, &sdspi_handle));
	host.slot = sdspi_handle;

	esp_err_t err = sdmmc_card_init(&host, &card);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Physical SD card init failed: %s (LittleFS at %s still works)",
		         esp_err_to_name(err), mountpoint);
		return;
	}
	ESP_LOGI(TAG, "Physical SD card detected, mounting at /sd");
	sdmmc_card_print_info(stdout, &card);
	// Now that the card is identified, bump to 10 MHz for bulk transfers.
	sdspi_host_set_card_clk(sdspi_handle, 10000);

	blkcache_config_t bcfg = {
		.blksize = 1024 * 32,
		.blkcount = 16,
		.read_sectors_cb = (read_sectors_t)sdmmc_read_sectors,
		.write_sectors_cb = (write_sectors_t)sdmmc_write_sectors,
		.arg = (void *)&card
	};
	blkcache_init(&bcfg, &bc);

	ff_diskio_impl_t discio = {
		.init = dio_init,
		.status = dio_status,
		.read = dio_read,
		.write = dio_write,
		.ioctl = dio_ioctl
	};

	BYTE pdrv = 0xFF;
	if (ff_diskio_get_drive(&pdrv) != ESP_OK) {
		ESP_LOGE(TAG, "Out of drive numbers");
		return;
	}
	ff_diskio_register(pdrv, &discio);

	char drv[3] = {'0' + pdrv, ':', 0};
	esp_vfs_fat_conf_t conf = {
		.base_path = "/sd",   // physical SD card lives at /sd
		.fat_drive = drv,
		.max_files = files,
	};
	ESP_ERROR_CHECK(esp_vfs_fat_register_cfg(&conf, &fatfs));

	FRESULT fr = f_mount(fatfs, drv, 1);
	if (fr != FR_OK) {
		ESP_LOGE(TAG, "f_mount failed %d", fr);
	}
}
