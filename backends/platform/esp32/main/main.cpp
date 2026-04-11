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
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "posixesp-fs-factory.h"
#include "tdeck_board.h"
#include "tdeck_kbd.h"
#include "tdeck_trackball.h"

#define TAG "main"

#include "../../../../../config.h"

#include "common/scummsys.h"

#include "backends/modular-backend.h"
#include "esp-mutex.h"
#include "base/main.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "common/config-manager.h"
#include "esp-graphics.h"
#include "esp-mixer.h"
#include "gui/debugger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "mmc.h"

class OSystem_esp32 : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_esp32(bool silenceLogs);
	virtual ~OSystem_esp32();

	virtual void initBackend();

	virtual bool pollEvent(Common::Event &event);

	virtual Common::MutexInternal *createMutex();
	virtual uint32 getMillis(bool skipRecord = false);
	virtual void delayMillis(uint msecs);
	virtual void getTimeAndDate(TimeDate &td, bool skipRecord = false) const;

	virtual void quit();

	virtual void logMessage(LogMessageType::Type type, const char *message);

	virtual void addSysArchivesToSearchSet(Common::SearchSet &s, int priority);

protected:
	virtual Common::Path getDefaultConfigFileName() override;
	virtual Common::Path getDefaultLogFileName() override;

private:
	timeval _startTime;
	bool _silenceLogs;
	Common::Point _mousePos;
	int64_t _last_input_poll_us;
};

OSystem_esp32::OSystem_esp32(bool silenceLogs) :
	_silenceLogs(silenceLogs) {
	_fsFactory = new POSIXESPFilesystemFactory();
	_last_input_poll_us = 0;
	_mousePos = Common::Point(0, 0);
}

OSystem_esp32::~OSystem_esp32() {
}


void OSystem_esp32::initBackend() {
	gettimeofday(&_startTime, 0);

	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = new DefaultSaveFileManager();
	EspGraphicsManager *gfx = new EspGraphicsManager();
	_graphicsManager = gfx;
	gfx->init();
	_mixerManager = new EspMixerManager(44100, 2048);
	_mixerManager->init();

	ConfMan.registerDefault("extrapath", Common::Path("/sdcard/scummvm/extras/"));
	ConfMan.registerDefault("iconspath", Common::Path("/sdcard/scummvm/icons/"));
	ConfMan.registerDefault("pluginspath", Common::Path("/sdcard/scummvm/plugins/"));
	ConfMan.registerDefault("savepath", Common::Path("/sdcard/scummvm/saves/"));
	ConfMan.registerDefault("themepath", Common::Path("/sdcard/scummvm/"));
	// Pin the GUI theme to one we actually ship in SPIFFS, so ScummVM
	// doesn't fall back to its built-in theme — that one is too tall
	// for our 320x240 panel and pushes dialog buttons off the bottom.
	ConfMan.registerDefault("gui_theme", "scummmodern");
	ConfMan.registerDefault("gui_renderer", "normal");
	// Open the file browser at the games directory by default so the
	// user doesn't have to navigate from / through SPIFFS' minimal
	// directory listing.
	ConfMan.registerDefault("browser_lastpath", "/sdcard/games");

	BaseBackend::initBackend();

	// Pre-add (or overwrite) the bundled MI1 EGA demo so the launcher
	// shows it without needing the file browser. Always overwrites in
	// case a stale config from an earlier boot points at a missing path.
	{
		const char *gameDomain = "monkey-demo";
		const char *gamePath = "/sdcard/games/monkey1";
		struct stat st;
		if (stat("/sdcard/games/monkey1/000.lfl", &st) == 0) {
			if (ConfMan.hasGameDomain(gameDomain)) {
				ConfMan.removeGameDomain(gameDomain);
			}
			ConfMan.addGameDomain(gameDomain);
			ConfMan.set("engineid",    "scumm",                       gameDomain);
			ConfMan.set("gameid",      "monkey",                      gameDomain);
			ConfMan.set("description", "Monkey Island 1 (EGA Demo)",  gameDomain);
			ConfMan.set("path",        gamePath,                       gameDomain);
			ConfMan.set("platform",    "pc",                           gameDomain);
			ConfMan.set("language",    "en",                           gameDomain);
			ConfMan.set("extra",       "Demo",                         gameDomain);
			ConfMan.set("guioptions",  "lang_English sndNoSpeech",     gameDomain);
			ConfMan.flushToDisk();
			ESP_LOGI(TAG, "Pre-registered Monkey Island 1 EGA demo at %s", gamePath);
		} else {
			ESP_LOGW(TAG, "MI1 demo data not found at %s", gamePath);
		}
	}
}

// Map the BBQ10 raw byte (mostly ASCII) to a (KeyCode, ascii) pair.
// The BBQ10 firmware reports ASCII for letters/digits/punctuation, and
// a handful of dedicated codes for the control keys. The numbers here
// match the arturo182 keyboard firmware used by the T-Deck.
//
// Special keys:
//   0x08 = backspace, 0x0a/0x0d = enter, 0x1b = escape, ' '   = space
//   0x81..0x84 = left/up/right/down, 0x06 = sym, 0x11/0x12 = shift/alt
//   0x03 = speaker (mapped to F5 = save), 0x05 = mic (F7 = load menu)
static void mapBbq10(uint8_t raw, Common::KeyCode &kc, uint16 &ascii) {
	kc = Common::KEYCODE_INVALID;
	ascii = 0;
	if (raw >= 'a' && raw <= 'z') {
		kc = (Common::KeyCode)(Common::KEYCODE_a + (raw - 'a'));
		ascii = raw;
		return;
	}
	if (raw >= 'A' && raw <= 'Z') {
		kc = (Common::KeyCode)(Common::KEYCODE_a + (raw - 'A'));
		ascii = raw;
		return;
	}
	if (raw >= '0' && raw <= '9') {
		kc = (Common::KeyCode)(Common::KEYCODE_0 + (raw - '0'));
		ascii = raw;
		return;
	}
	switch (raw) {
	case 0x08: kc = Common::KEYCODE_BACKSPACE; ascii = Common::ASCII_BACKSPACE; return;
	case 0x0a:
	case 0x0d: kc = Common::KEYCODE_RETURN; ascii = Common::ASCII_RETURN; return;
	case 0x1b: kc = Common::KEYCODE_ESCAPE; ascii = Common::ASCII_ESCAPE; return;
	case ' ':  kc = Common::KEYCODE_SPACE; ascii = ' '; return;
	case '.':  kc = Common::KEYCODE_PERIOD; ascii = '.'; return;
	case ',':  kc = Common::KEYCODE_COMMA; ascii = ','; return;
	case '?':  kc = Common::KEYCODE_SLASH; ascii = '?'; return;
	case '!':  kc = Common::KEYCODE_1; ascii = '!'; return;
	case '@':  kc = Common::KEYCODE_2; ascii = '@'; return;
	case '#':  kc = Common::KEYCODE_3; ascii = '#'; return;
	case '$':  kc = Common::KEYCODE_4; ascii = '$'; return;
	case '%':  kc = Common::KEYCODE_5; ascii = '%'; return;
	case '^':  kc = Common::KEYCODE_6; ascii = '^'; return;
	case '&':  kc = Common::KEYCODE_7; ascii = '&'; return;
	case '*':  kc = Common::KEYCODE_8; ascii = '*'; return;
	case '(':  kc = Common::KEYCODE_9; ascii = '('; return;
	case ')':  kc = Common::KEYCODE_0; ascii = ')'; return;
	case '-':  kc = Common::KEYCODE_MINUS; ascii = '-'; return;
	case '_':  kc = Common::KEYCODE_UNDERSCORE; ascii = '_'; return;
	case '=':  kc = Common::KEYCODE_EQUALS; ascii = '='; return;
	case '+':  kc = Common::KEYCODE_PLUS; ascii = '+'; return;
	case '/':  kc = Common::KEYCODE_SLASH; ascii = '/'; return;
	case '\\': kc = Common::KEYCODE_BACKSLASH; ascii = '\\'; return;
	case '\'': kc = Common::KEYCODE_QUOTE; ascii = '\''; return;
	case '"':  kc = Common::KEYCODE_QUOTEDBL; ascii = '"'; return;
	case ':':  kc = Common::KEYCODE_COLON; ascii = ':'; return;
	case ';':  kc = Common::KEYCODE_SEMICOLON; ascii = ';'; return;
	// Arrow keys (BBQ10 reports the printable glyphs on these buttons)
	case 0x81: kc = Common::KEYCODE_LEFT; return;
	case 0x82: kc = Common::KEYCODE_UP; return;
	case 0x83: kc = Common::KEYCODE_DOWN; return;
	case 0x84: kc = Common::KEYCODE_RIGHT; return;
	// Dedicated shortcut keys on the T-Deck
	case 0x03: kc = Common::KEYCODE_F5; ascii = Common::ASCII_F5; return; // speaker -> save menu
	case 0x05: kc = Common::KEYCODE_F7; ascii = Common::ASCII_F7; return; // mic     -> load menu
	case 0x06: kc = Common::KEYCODE_LALT; return;                        // sym     -> alt
	case 0x11: kc = Common::KEYCODE_LSHIFT; return;
	case 0x12: kc = Common::KEYCODE_LALT; return;
	default: return;
	}
}

bool OSystem_esp32::pollEvent(Common::Event &event) {
	((DefaultTimerManager *)getTimerManager())->checkTimers();

	event.type = Common::EVENT_INVALID;

	// 1. Drain the BBQ10 keyboard queue first so typing feels responsive.
	tdeck_kbd_event_t k;
	if (tdeck_kbd_poll(&k)) {
		Common::KeyCode kc;
		uint16 ascii;
		mapBbq10(k.raw, kc, ascii);
		if (kc != Common::KEYCODE_INVALID) {
			event.type = k.pressed ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbd.keycode = kc;
			event.kbd.ascii = ascii;
			event.kbd.flags = 0;
			return true;
		}
	}

	// 2. Trackball: poll at most every 1/60 s so we don't spam EVENT_MOUSEMOVE.
	if ((esp_timer_get_time() - _last_input_poll_us) > (1000000 / 60)) {
		_last_input_poll_us = esp_timer_get_time();
		tdeck_trackball_state_t tb;
		tdeck_trackball_poll(&tb);

		// Click edges take precedence so a tap isn't swallowed.
		if (tb.click_down == 1) {
			event.type = Common::EVENT_LBUTTONDOWN;
			event.mouse = _mousePos;
			return true;
		}
		if (tb.click_down == -1) {
			event.type = Common::EVENT_LBUTTONUP;
			event.mouse = _mousePos;
			return true;
		}

		if (tb.dx != 0 || tb.dy != 0) {
			int newX = _mousePos.x + tb.dx;
			int newY = _mousePos.y + tb.dy;
			int maxX = 0, maxY = 0;
			if (_graphicsManager && _graphicsManager->isOverlayVisible()) {
				maxX = _graphicsManager->getOverlayWidth() - 1;
				maxY = _graphicsManager->getOverlayHeight() - 1;
			} else if (_graphicsManager && _graphicsManager->getWidth() > 0) {
				maxX = _graphicsManager->getWidth() - 1;
				maxY = _graphicsManager->getHeight() - 1;
			} else if (_graphicsManager) {
				maxX = _graphicsManager->getOverlayWidth() - 1;
				maxY = _graphicsManager->getOverlayHeight() - 1;
			}
			if (newX < 0) newX = 0;
			if (newY < 0) newY = 0;
			if (newX > maxX) newX = maxX;
			if (newY > maxY) newY = maxY;
			_mousePos = Common::Point(newX, newY);
			event.type = Common::EVENT_MOUSEMOVE;
			event.mouse = _mousePos;
			// Keep the graphics manager's software cursor in sync so
			// updateScreen draws it at the right spot.
			if (_graphicsManager) {
				_graphicsManager->warpMouse(newX, newY);
			}
			return true;
		}
	}

	return false;
}

Common::MutexInternal *OSystem_esp32::createMutex() {
	return createEspMutexInternal();
}

uint32 OSystem_esp32::getMillis(bool skipRecord) {
	uint64_t t_us=esp_timer_get_time();
	return (uint32)(t_us/1000ULL);
}

void OSystem_esp32::delayMillis(uint msecs) {
//	ESP_LOGI(TAG, "delayMillis %d", msecs);
	vTaskDelay(pdMS_TO_TICKS(msecs));
}

void OSystem_esp32::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	time_t curTime = time(0);
	struct tm t = *localtime(&curTime);
	td.tm_sec = t.tm_sec;
	td.tm_min = t.tm_min;
	td.tm_hour = t.tm_hour;
	td.tm_mday = t.tm_mday;
	td.tm_mon = t.tm_mon;
	td.tm_year = t.tm_year;
	td.tm_wday = t.tm_wday;
}

void OSystem_esp32::quit() {
	exit(0);
}

void OSystem_esp32::logMessage(LogMessageType::Type type, const char *message) {
	if (_silenceLogs)
		return;

	FILE *output = 0;

	if (type == LogMessageType::kInfo || type == LogMessageType::kDebug)
		output = stdout;
	else
		output = stderr;

	fputs(message, output);
	fflush(output);
}

void OSystem_esp32::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	s.add("engine-data", new Common::FSDirectory("/sdcard/scummvm/", 4), priority);
	s.add("gui/themes", new Common::FSDirectory("/sdcard/scummvm/", 4), priority);
}

Common::Path OSystem_esp32::getDefaultConfigFileName() {
	return "/sdcard/scummvm/scummvm.ini";
}

Common::Path OSystem_esp32::getDefaultLogFileName() {
	return "/sdcard/scummvm/scummvm.log";
}

OSystem *OSystem_esp32_create(bool silenceLogs) {
	return new OSystem_esp32(silenceLogs);
}

extern "C" {

void main_task(void *param) {
	// Invoke the actual ScummVM main entry point:
//	const char *argv[]={"scummvm", "-d", "11"};
	const char *argv[]={"scummvm"};
	int res = scummvm_main(sizeof(argv)/sizeof(argv[0]), argv);
	ESP_LOGW(TAG, "Scummvm_main done");
	g_system->destroy();
}

int app_main() {
	tdeck_board_init();

	// Reserve the two internal-RAM-only allocations up front before
	// anything else can fragment or consume the internal heap:
	//   * 150 KB DMA-capable panel framebuffer for the LCD
	//   * 64 KB main task stack (must be internal because flash/SPIFFS
	//     operations disable the PSRAM cache while they run)
	// Both together fit in the ~243 KB usable internal SRAM only if
	// they get first dibs on the heap.
	EspGraphicsManager::preallocatePanelFb();

	const int stack_depth = 64 * 1024;
	StaticTask_t *taskbuf = (StaticTask_t *)heap_caps_calloc(
		1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	uint8_t *stackbuf = (uint8_t *)heap_caps_calloc(
		stack_depth, 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	assert(taskbuf && stackbuf);

	tdeck_kbd_init();
	tdeck_trackball_init();
	sdcard_mount_blkcache("/sdcard", 15);

	g_system = OSystem_esp32_create(false);
	assert(g_system);

	xTaskCreateStaticPinnedToCore(main_task, "main", stack_depth, NULL, 2,
	                              (StackType_t *)stackbuf, taskbuf, 0);
	return 0;
}

} //extern c