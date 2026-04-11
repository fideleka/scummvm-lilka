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

#include "common/config-manager.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/translation.h"
#include "common/memstream.h"
#include "engines/engine.h"
#include "graphics/blit.h"
#include "gui/ThemeEngine.h"
#include "image/image_decoder.h"
#include "image/png.h"

#include "esp-graphics.h"
#include "tdeck_board.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <string.h>

#define TAG "EspGraphics"

#define PANEL_W  TDECK_LCD_H_RES
#define PANEL_H  TDECK_LCD_V_RES

extern const uint8_t loading_png_start[] asm("_binary_loading_png_start");
extern const uint8_t loading_png_end[] asm("_binary_loading_png_end");

// See the longer comment next to the definition below. Byte-swapped
// RGB565 because ST7789 wants MSB-first on the wire.
static inline uint16_t rgb_to_565(int r, int g, int b) {
	uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	return (uint16_t)((v >> 8) | (v << 8));
}

// Panel framebuffer pre-allocated from app_main() in PSRAM. Claimed via
// takePreallocatedPanelFb() in init(). The framebuffer doesn't need to
// be DMA-capable because we always copy the rows we want to send to the
// panel into a small DMA-capable internal-RAM strip first (s_dma_strip).
static uint16_t *s_preallocated_panelfb = nullptr;

void EspGraphicsManager::preallocatePanelFb() {
	if (s_preallocated_panelfb) return;
	size_t bytes = (size_t)TDECK_LCD_H_RES * TDECK_LCD_V_RES * sizeof(uint16_t);
	s_preallocated_panelfb = (uint16_t *)heap_caps_calloc(
		1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

uint16_t *EspGraphicsManager::takePreallocatedPanelFb() {
	uint16_t *p = s_preallocated_panelfb;
	s_preallocated_panelfb = nullptr;
	return p;
}

// DMA-capable internal-RAM strip used to stream rows of the panel FB
// to the LCD. 64 rows of 320 RGB565 pixels = 40960 bytes. Sized once
// in init() and reused for every flush. Larger strip = fewer SPI
// transactions per frame = less chance of queue races.
#define DMA_STRIP_ROWS 64
static uint16_t *s_dma_strip = nullptr;

// Two DMA-capable strip buffers, used in ping-pong so the next chunk
// can be filled while the previous chunk is still being DMA'd to the
// panel. Each is DMA_STRIP_ROWS * panelW * 2 bytes (64 * 320 * 2 = 40 KB).
static uint16_t *s_dma_strip_b = nullptr;

// Wait-for-completion semaphore. Given by an esp_lcd event callback
// when the bus has finished with one of the two strip buffers.
static SemaphoreHandle_t s_lcd_done_sem = nullptr;
static volatile int s_lcd_in_flight = 0;

static bool IRAM_ATTR lcd_done_cb(esp_lcd_panel_io_handle_t io,
                                  esp_lcd_panel_io_event_data_t *edata,
                                  void *user_ctx) {
	BaseType_t hpw = pdFALSE;
	xSemaphoreGiveFromISR(s_lcd_done_sem, &hpw);
	return hpw == pdTRUE;
}

// Stream a rectangular region of _panelfb to the panel using ping-pong
// strip buffers, ensuring we never mutate a buffer that DMA is still
// reading from.
static void streamRectToPanel(esp_lcd_panel_handle_t panel,
                              const uint16_t *src_fb, int src_pitch_px,
                              int x0, int y0, int x1, int y1) {
	if (x1 <= x0 || y1 <= y0) return;
	int w = x1 - x0;
	int which = 0;
	for (int y = y0; y < y1; y += DMA_STRIP_ROWS) {
		int rows = (y + DMA_STRIP_ROWS > y1) ? (y1 - y) : DMA_STRIP_ROWS;
		// Wait until we have a free slot in the panel-io trans queue.
		// We always have at most ONE outstanding transfer per buffer
		// because of the ping-pong, and at most TWO in flight total.
		while (s_lcd_in_flight >= 2) {
			xSemaphoreTake(s_lcd_done_sem, portMAX_DELAY);
			s_lcd_in_flight--;
		}
		uint16_t *strip = (which == 0) ? s_dma_strip : s_dma_strip_b;
		which ^= 1;
		for (int r = 0; r < rows; r++) {
			memcpy(&strip[r * w],
			       &src_fb[(y + r) * src_pitch_px + x0],
			       w * sizeof(uint16_t));
		}
		s_lcd_in_flight++;
		esp_lcd_panel_draw_bitmap(panel, x0, y, x1, y + rows, strip);
	}
	// Drain remaining transfers before returning so the caller can
	// safely modify _panelfb again.
	while (s_lcd_in_flight > 0) {
		xSemaphoreTake(s_lcd_done_sem, portMAX_DELAY);
		s_lcd_in_flight--;
	}
}

bool EspGraphicsManager::hasFeature(OSystem::Feature f) const {
	if (f == OSystem::kFeatureNoQuit) return true;
	if (f == OSystem::kFeatureCursorPalette) return true;
	return false;
}

void EspGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
}

bool EspGraphicsManager::getFeatureState(OSystem::Feature f) const {
	return false;
}

void EspGraphicsManager::init() {
	tdeck_board_init();

	// Panel IO over shared SPI.
	esp_lcd_panel_io_spi_config_t io_config = {};
	io_config.cs_gpio_num = TDECK_LCD_CS_GPIO;
	io_config.dc_gpio_num = TDECK_LCD_DC_GPIO;
	io_config.spi_mode = TDECK_LCD_SPI_MODE;
	io_config.pclk_hz = TDECK_LCD_PIXEL_CLOCK_HZ;
	io_config.trans_queue_depth = 10;
	io_config.lcd_cmd_bits = TDECK_LCD_CMD_BITS;
	io_config.lcd_param_bits = TDECK_LCD_PARAM_BITS;
	// The panel framebuffer is stored pre-byte-swapped by rgb_to_565(),
	// so we don't need esp_lcd to do any swapping.
	io_config.flags.lsb_first = 0;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TDECK_SPI_HOST,
	                                         &io_config, &_io_handle));

	esp_lcd_panel_dev_config_t panel_config = {};
	panel_config.reset_gpio_num = TDECK_LCD_RST_GPIO;
	panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
	panel_config.bits_per_pixel = TDECK_LCD_BITS_PER_PIXEL;
	ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(_io_handle, &panel_config, &_panel_handle));

	ESP_ERROR_CHECK(esp_lcd_panel_reset(_panel_handle));
	ESP_ERROR_CHECK(esp_lcd_panel_init(_panel_handle));
	ESP_ERROR_CHECK(esp_lcd_panel_invert_color(_panel_handle, true));
	// Landscape: 320x240 with the T-Deck in its natural orientation.
	// After swap_xy(true), the MX bit of the ST7789 MADCTL register
	// ends up controlling the visual Y axis and MY controls visual X.
	// T-Deck v1 needs MX on, MY off.
	ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(_panel_handle, true));
	ESP_ERROR_CHECK(esp_lcd_panel_mirror(_panel_handle, true, false));
	ESP_ERROR_CHECK(esp_lcd_panel_set_gap(_panel_handle, 0, 0));
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(_panel_handle, true));

	// Backlight on.
	gpio_config_t bl_cfg = {};
	bl_cfg.pin_bit_mask = 1ULL << TDECK_LCD_BL_GPIO;
	bl_cfg.mode = GPIO_MODE_OUTPUT;
	ESP_ERROR_CHECK(gpio_config(&bl_cfg));
	gpio_set_level(TDECK_LCD_BL_GPIO, 1);

	// Panel framebuffer lives in PSRAM (cheap, plentiful). Real DMA
	// transfers go through s_dma_strip below.
	_panelfb = takePreallocatedPanelFb();
	size_t fb_bytes = (size_t)_panelW * _panelH * sizeof(uint16_t);
	if (!_panelfb) {
		ESP_LOGW(TAG, "Panel FB not pre-allocated, allocating from PSRAM");
		_panelfb = (uint16_t *)heap_caps_calloc(1, fb_bytes,
			MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	}
	assert(_panelfb && "panel framebuffer allocation failed");
	memset(_panelfb, 0, fb_bytes);

	// Allocate the two ping-pong DMA strips used by streamRectToPanel.
	size_t strip_bytes = (size_t)_panelW * DMA_STRIP_ROWS * sizeof(uint16_t);
	s_dma_strip   = (uint16_t *)heap_caps_malloc(strip_bytes,
		MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	s_dma_strip_b = (uint16_t *)heap_caps_malloc(strip_bytes,
		MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	assert(s_dma_strip && s_dma_strip_b && "LCD DMA strip allocation failed");
	s_lcd_done_sem = xSemaphoreCreateCounting(8, 0);
	assert(s_lcd_done_sem);

	// Hook the panel-io completion callback so we can throttle.
	esp_lcd_panel_io_callbacks_t cbs = {};
	cbs.on_color_trans_done = lcd_done_cb;
	esp_lcd_panel_io_register_event_callbacks(_io_handle, &cbs, NULL);

	_panelLock = xSemaphoreCreateMutex();

	// Clear the panel framebuffer to black before the splash draw below.
	memset(_panelfb, 0, (size_t)_panelW * _panelH * sizeof(uint16_t));

	// Draw the "Loading..." splash.
	{
		Image::PNGDecoder d;
		Common::MemoryReadStream str(loading_png_start, loading_png_end - loading_png_start);
		if (d.loadStream(str)) {
			const Graphics::Surface *s = d.getSurface();
			int origX = (_panelW - s->w) / 2;
			int origY = (_panelH - s->h) / 2;
			if (origX < 0) origX = 0;
			if (origY < 0) origY = 0;
			int drawW = (s->w < _panelW) ? s->w : _panelW;
			int drawH = (s->h < _panelH) ? s->h : _panelH;
			const uint8_t *p = (const uint8_t *)s->getPixels();
			int pitch = s->pitch;
			for (int y = 0; y < drawH; y++) {
				const uint8_t *src = p + y * pitch;
				uint16_t *dst = &_panelfb[(origY + y) * _panelW + origX];
				for (int x = 0; x < drawW; x++) {
					int r = *src++;
					int g = *src++;
					int b = *src++;
					src++;
					*dst++ = rgb_to_565(r, g, b);
				}
			}
		streamRectToPanel(_panel_handle, _panelfb, _panelW, 0, 0, _panelW, _panelH);
		}
	}

	_overlay.create(_panelW, _panelH, getOverlayFormat());

	// Default mapping until initSize() is called.
	_dstX = 0;
	_dstY = 0;
	_dstW = _panelW;
	_dstH = _panelH;
}

void EspGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	ESP_LOGI(TAG, "EspGraphicsManager::initSize %u %u", width, height);

	_width = width;
	_height = height;
	_format = format ? *format : Graphics::PixelFormat::createFormatCLUT8();

	_surf.free();
	_surf.create(width, height, _format);

	// Compute letterbox / downscale mapping.
	// Preserve aspect ratio; center on the panel.
	int dstW = _panelW;
	int dstH = (int)((int64_t)_panelW * height / width);
	if (dstH > _panelH) {
		dstH = _panelH;
		dstW = (int)((int64_t)_panelH * width / height);
	}
	_dstW = dstW;
	_dstH = dstH;
	_dstX = (_panelW - _dstW) / 2;
	_dstY = (_panelH - _dstH) / 2;

	// Blank panel framebuffer (and push once to clear any previous content).
	memset(_panelfb, 0, (size_t)_panelW * _panelH * sizeof(uint16_t));
	if (_panel_handle) {
		streamRectToPanel(_panel_handle, _panelfb, _panelW, 0, 0, _panelW, _panelH);
	}

	_dirty = Common::Rect(0, 0, width, height);
	_palDirty = true;
}

Graphics::Surface *EspGraphicsManager::lockScreen() {
	return &_surf;
}

void EspGraphicsManager::unlockScreen() {
	_dirty = Common::Rect(0, 0, _surf.w, _surf.h);
}

void EspGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	_surf.copyRectToSurface(buf, pitch, x, y, w, h);
	if (_dirty.isEmpty()) {
		_dirty = Common::Rect(x, y, x + w, y + h);
	} else {
		if (_dirty.top > y) _dirty.top = y;
		if (_dirty.bottom < y + h) _dirty.bottom = y + h;
		if (_dirty.left > x) _dirty.left = x;
		if (_dirty.right < x + w) _dirty.right = x + w;
	}
}

void EspGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	for (uint i = 0; i < num; i++) {
		if (start + i >= 256) break;
		_pal[(start + i) * 3 + 0] = colors[i * 3 + 0];
		_pal[(start + i) * 3 + 1] = colors[i * 3 + 1];
		_pal[(start + i) * 3 + 2] = colors[i * 3 + 2];
	}
	_palDirty = true;
	_dirty = Common::Rect(0, 0, _surf.w, _surf.h);
}

void EspGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	for (uint i = 0; i < num * 3; i++) {
		colors[i] = _pal[start * 3 + i];
	}
}

void EspGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	_overlay.copyRectToSurface(buf, pitch, x, y, w, h);
}

void EspGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	surface.copyFrom(_overlay);
}

int16 EspGraphicsManager::getOverlayHeight() const { return _panelH; }
int16 EspGraphicsManager::getOverlayWidth()  const { return _panelW; }

void EspGraphicsManager::clearOverlay() {
	_overlay.fillRect(Common::Rect(0, 0, _overlay.w, _overlay.h), 0);
}

void EspGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY,
                                        uint32 keycolor, bool dontScale,
                                        const Graphics::PixelFormat *format, const byte *mask) {
	free(_cursorData);
	_cursorData = nullptr;
	_cursorW = w;
	_cursorH = h;
	_cursorHotX = hotspotX;
	_cursorHotY = hotspotY;
	_cursorKey = keycolor;
	if (buf && w && h) {
		// We only support CLUT8 cursors here (the common case). Anything
		// else we ignore — ScummVM will still work, the cursor will just
		// be missing.
		if (!format || format->bytesPerPixel == 1) {
			_cursorData = (byte *)malloc(w * h);
			memcpy(_cursorData, buf, w * h);
		}
	}
}

void EspGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	for (uint i = 0; i < num; i++) {
		if (start + i >= 256) break;
		_cursorPal[(start + i) * 3 + 0] = colors[i * 3 + 0];
		_cursorPal[(start + i) * 3 + 1] = colors[i * 3 + 1];
		_cursorPal[(start + i) * 3 + 2] = colors[i * 3 + 2];
	}
	_cursorHasPal = true;
}

// Byte-swapped RGB565 for the ST7789 on-wire format — see forward
// declaration near the top of the file.

void EspGraphicsManager::drawCursorInto(uint16_t *fb, int fb_w, int fb_h,
                                        int originX, int originY, int scaleW, int scaleH) {
	if (!_cursorVisible || !_cursorData || _cursorW == 0 || _cursorH == 0) return;

	// Force the cursor to render in a high-contrast bright yellow so
	// it's always visible against game graphics. EGA-era games use very
	// dark palettes for their native cursors and they vanish on a tiny
	// LCD when shown 1:1.
	const uint16_t kFillColor = rgb_to_565(255, 255, 0); // bright yellow

	// Choose the source coordinate space we're scaling from. When the
	// overlay is up the cursor coords are in panel/overlay pixels (1:1
	// with the destination, so no scaling). When a game is running,
	// they're in game pixels and we scale into the dst rect.
	int srcW, srcH;
	if (_overlayVisible) {
		srcW = _panelW;
		srcH = _panelH;
	} else {
		srcW = (int)_width;
		srcH = (int)_height;
	}
	if (srcW <= 0 || srcH <= 0) return;

	int gx = _cursorX - _cursorHotX;
	int gy = _cursorY - _cursorHotY;
	for (uint cy = 0; cy < _cursorH; cy++) {
		int srcY = gy + cy;
		if (srcY < 0 || srcY >= srcH) continue;
		int panelYStart = originY + (srcY * scaleH) / srcH;
		int panelYEnd   = originY + ((srcY + 1) * scaleH) / srcH;
		if (panelYEnd <= panelYStart) panelYEnd = panelYStart + 1;
		for (uint cx = 0; cx < _cursorW; cx++) {
			int srcX = gx + cx;
			if (srcX < 0 || srcX >= srcW) continue;
			byte idx = _cursorData[cy * _cursorW + cx];
			if (idx == _cursorKey) continue;
			// Override the cursor color: bright yellow body so it's
			// always visible against any game art (the original EGA
			// cursor is too dark on a tiny LCD).
			uint16_t col = kFillColor;
			int panelXStart = originX + (srcX * scaleW) / srcW;
			int panelXEnd   = originX + ((srcX + 1) * scaleW) / srcW;
			if (panelXEnd <= panelXStart) panelXEnd = panelXStart + 1;
			for (int py = panelYStart; py < panelYEnd && py < fb_h; py++) {
				if (py < 0) continue;
				uint16_t *row = &fb[py * fb_w];
				for (int px = panelXStart; px < panelXEnd && px < fb_w; px++) {
					if (px < 0) continue;
					row[px] = col;
				}
			}
		}
	}
}

void EspGraphicsManager::flushToPanel(const Common::Rect &r) {
	if (r.isEmpty()) return;
	// Map dirty game rect to panel rect, expanded to cover nearest-neighbour rounding.
	int px0 = _dstX + (r.left  * _dstW) / (int)_width;
	int py0 = _dstY + (r.top   * _dstH) / (int)_height;
	int px1 = _dstX + ((r.right  * _dstW + _width  - 1) / (int)_width);
	int py1 = _dstY + ((r.bottom * _dstH + _height - 1) / (int)_height);
	if (px0 < 0) px0 = 0;
	if (py0 < 0) py0 = 0;
	if (px1 > _panelW) px1 = _panelW;
	if (py1 > _panelH) py1 = _panelH;
	if (px1 <= px0 || py1 <= py0) return;

	streamRectToPanel(_panel_handle, _panelfb, _panelW, px0, py0, px1, py1);
}

void EspGraphicsManager::updateScreen() {
	// Rate limit to 30 Hz.
	int64_t now = esp_timer_get_time();
	if (now - _last_time_updated < 1000000 / 30) return;
	_last_time_updated = now;

	xSemaphoreTake(_panelLock, portMAX_DELAY);

	if (_overlayVisible) {
		// Overlay is authoritative: copy the whole overlay surface into the
		// panel FB 1:1 (overlay is sized to the panel).
		memcpy(_panelfb, _overlay.getPixels(), (size_t)_panelW * _panelH * sizeof(uint16_t));
		drawCursorInto(_panelfb, _panelW, _panelH, 0, 0, _panelW, _panelH);
		streamRectToPanel(_panel_handle, _panelfb, _panelW, 0, 0, _panelW, _panelH);
		xSemaphoreGive(_panelLock);
		return;
	}

	if (_width == 0 || _height == 0) {
		xSemaphoreGive(_panelLock);
		return;
	}

	if (_palDirty && _format.bytesPerPixel == 1) {
		for (int i = 0; i < 256; i++) {
			_pal16[i] = rgb_to_565(_pal[i * 3 + 0], _pal[i * 3 + 1], _pal[i * 3 + 2]);
		}
		_palDirty = false;
	}

	// If a cursor is visible we need to redraw everything the previous frame
	// might have touched, so expand the dirty rect to the whole screen when
	// the cursor moved (simplest safe approach for now).
	Common::Rect dirty = _dirty;
	if (_cursorVisible) {
		dirty = Common::Rect(0, 0, _width, _height);
	}
	if (dirty.isEmpty()) {
		xSemaphoreGive(_panelLock);
		return;
	}

	// Clamp dirty to game bounds.
	if (dirty.left < 0) dirty.left = 0;
	if (dirty.top < 0) dirty.top = 0;
	if (dirty.right > (int)_width) dirty.right = _width;
	if (dirty.bottom > (int)_height) dirty.bottom = _height;

	// Nearest-neighbour blit the dirty region into the panel framebuffer.
	if (_format.bytesPerPixel == 1) {
		const uint8_t *srcBase = (const uint8_t *)_surf.getPixels();
		int srcPitch = _surf.pitch;
		for (int sy = dirty.top; sy < dirty.bottom; sy++) {
			int py0 = _dstY + (sy * _dstH) / (int)_height;
			int py1 = _dstY + ((sy + 1) * _dstH) / (int)_height;
			if (py1 <= py0) py1 = py0 + 1;
			if (py0 < 0) py0 = 0;
			if (py1 > _panelH) py1 = _panelH;
			const uint8_t *srcRow = srcBase + sy * srcPitch;
			for (int sx = dirty.left; sx < dirty.right; sx++) {
				uint16_t col = _pal16[srcRow[sx]];
				int px0 = _dstX + (sx * _dstW) / (int)_width;
				int px1 = _dstX + ((sx + 1) * _dstW) / (int)_width;
				if (px1 <= px0) px1 = px0 + 1;
				if (px0 < 0) px0 = 0;
				if (px1 > _panelW) px1 = _panelW;
				for (int py = py0; py < py1; py++) {
					uint16_t *dst = &_panelfb[py * _panelW + px0];
					for (int px = px0; px < px1; px++) *dst++ = col;
				}
			}
		}
	} else if (_format.bytesPerPixel == 2) {
		const uint16_t *srcBase = (const uint16_t *)_surf.getPixels();
		int srcPitchPx = _surf.pitch / 2;
		for (int sy = dirty.top; sy < dirty.bottom; sy++) {
			int py0 = _dstY + (sy * _dstH) / (int)_height;
			int py1 = _dstY + ((sy + 1) * _dstH) / (int)_height;
			if (py1 <= py0) py1 = py0 + 1;
			if (py0 < 0) py0 = 0;
			if (py1 > _panelH) py1 = _panelH;
			const uint16_t *srcRow = srcBase + sy * srcPitchPx;
			for (int sx = dirty.left; sx < dirty.right; sx++) {
				uint16_t col = srcRow[sx];
				int px0 = _dstX + (sx * _dstW) / (int)_width;
				int px1 = _dstX + ((sx + 1) * _dstW) / (int)_width;
				if (px1 <= px0) px1 = px0 + 1;
				if (px0 < 0) px0 = 0;
				if (px1 > _panelW) px1 = _panelW;
				for (int py = py0; py < py1; py++) {
					uint16_t *dst = &_panelfb[py * _panelW + px0];
					for (int px = px0; px < px1; px++) *dst++ = col;
				}
			}
		}
	}

	// Software cursor overlay on top of the dirty region.
	drawCursorInto(_panelfb, _panelW, _panelH, _dstX, _dstY, _dstW, _dstH);

	flushToPanel(dirty);
	_dirty = Common::Rect();

	int64_t dt = esp_timer_get_time() - now;
	if (dt > 33000) {
		ESP_LOGW(TAG, "updateScreen took %d us", (int)dt);
	}
	xSemaphoreGive(_panelLock);
}

void EspGraphicsManager::beginGFXTransaction() {}

OSystem::TransactionError EspGraphicsManager::endGFXTransaction() {
	return OSystem::kTransactionSuccess;
}
