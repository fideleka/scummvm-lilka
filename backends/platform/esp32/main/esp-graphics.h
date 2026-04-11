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

#ifndef BACKENDS_GRAPHICS_ESP_H
#define BACKENDS_GRAPHICS_ESP_H

#include "backends/graphics/graphics.h"
#include "graphics/surface.h"
#include "esp_timer.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

class EspGraphicsManager : public GraphicsManager {
public:
	virtual ~EspGraphicsManager() {}

	void init();
	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

#ifdef USE_RGB_COLOR
	Graphics::PixelFormat getScreenFormat() const override {
		return _format;
	}

	Common::List<Graphics::PixelFormat> getSupportedFormats() const override {
		Common::List<Graphics::PixelFormat> list;
		list.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11,  5,  0,  0));
		list.push_back(Graphics::PixelFormat::createFormatCLUT8());
		return list;
	}
#endif
	void initSize(uint width, uint height, const Graphics::PixelFormat *format = NULL) override;

	int getScreenChangeID() const override { return 0; }

	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override { return _height; }
	int16 getWidth() const override { return _width; }
	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;
	void fillScreen(uint32 col) override {}
	void fillScreen(const Common::Rect &r, uint32 col) override {}
	void updateScreen() override;
	void setShakePos(int shakeXOffset, int shakeYOffset) override {}
	void setFocusRectangle(const Common::Rect& rect) override {}
	void clearFocusRectangle() override {}

	void showOverlay(bool inGUI) override { _overlayVisible = true; }
	void hideOverlay() override { _overlayVisible = false; }
	bool isOverlayVisible() const override { return _overlayVisible; }
	// RGB565 as used in display
	Graphics::PixelFormat getOverlayFormat() const override { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override;
	int16 getOverlayWidth() const override;

	bool showMouse(bool visible) override {
		bool prev = _cursorVisible;
		_cursorVisible = visible;
		return prev;
	}
	void warpMouse(int x, int y) override {
		_cursorX = x;
		_cursorY = y;
	}
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY,
	                    uint32 keycolor, bool dontScale = false,
	                    const Graphics::PixelFormat *format = NULL,
	                    const byte *mask = NULL) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

private:
	static void gfxTaskStub(void *arg);
	void gfxTask();

	void flushToPanel(const Common::Rect &r);
	void drawCursorInto(uint16_t *fb, int fb_w, int fb_h,
	                    int originX, int originY, int scaleW, int scaleH);

	uint _width = 0;
	uint _height = 0;
	Graphics::PixelFormat _format;
	Graphics::Surface _surf;              // game framebuffer (CLUT8 or RGB565)
	byte _pal[256 * 3];
	uint16_t _pal16[256];
	bool _palDirty = true;
	Common::Rect _dirty;
	bool _overlayVisible = false;
	int64_t _last_time_updated = 0;

	esp_lcd_panel_handle_t _panel_handle = NULL;
	esp_lcd_panel_io_handle_t _io_handle = NULL;

	// Single full RGB565 framebuffer that mirrors what the LCD shows.
	// Sized to the physical panel (320x240) and allocated in internal
	// DMA-capable RAM so esp_lcd_panel_draw_bitmap can stream straight
	// from it.
	uint16_t *_panelfb = nullptr;
	int _panelW = 320;
	int _panelH = 240;

public:
	// Set the panel framebuffer from outside (before init()). Used by
	// app_main() to pre-allocate the 150 KB DMA-capable internal RAM
	// block while that RAM is still plentiful.
	static void preallocatePanelFb();
	static uint16_t *takePreallocatedPanelFb();
private:

	// Game-to-panel mapping (letterbox / downscale parameters computed
	// in initSize).
	int _dstX = 0;          // destination origin on panel
	int _dstY = 0;
	int _dstW = 0;          // destination area size on panel
	int _dstH = 0;

	// Overlay surface (always panel resolution, RGB565).
	Graphics::Surface _overlay;

	// Software mouse cursor.
	bool _cursorVisible = false;
	int _cursorX = 0;
	int _cursorY = 0;
	int _cursorHotX = 0;
	int _cursorHotY = 0;
	uint _cursorW = 0;
	uint _cursorH = 0;
	uint32 _cursorKey = 0;
	byte *_cursorData = nullptr;
	byte _cursorPal[256 * 3];
	bool _cursorHasPal = false;

	SemaphoreHandle_t _panelLock = nullptr;
	SemaphoreHandle_t _drawDone = nullptr;
};

#endif
