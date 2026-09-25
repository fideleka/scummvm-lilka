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
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"
#include <algorithm>

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
#include "launch_manifest.h"

static bool s_managedLaunch = false;
static LilkaButtonAction s_buttonActions[6] = {
	LilkaButtonAction::LeftClick, LilkaButtonAction::RightClick, LilkaButtonAction::F5,
	LilkaButtonAction::F7, LilkaButtonAction::Enter, LilkaButtonAction::VirtualKeyboard};
static int s_pointerSlowStep = 1;
static int s_pointerFastStep = 4;
static int s_pointerAccelerationUs = 500000;

// Follow the device-proven Lilka SDK USB detach/PHY handoff. ESP-IDF 5.3's
// esp_restart_noos_dig() is only linked on ESP32, so S3 uses the ROM system
// reset call. Select + Start return was confirmed on the first Lilka test.
[[noreturn]] static void returnToKeira() {
	CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
	vTaskDelay(pdMS_TO_TICKS(2000));
	CLEAR_PERI_REG_MASK(
		RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
	CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
	SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
	esp_rom_software_reset_system();
	while (true) {}
}

// Keep the escape chord responsive while ScummVM is loading or if its main
// task blocks. A panic stops the scheduler; its serial backtrace is still
// needed to diagnose that case.
static void exitChordTask(void *) {
	int64_t heldSinceUs = 0;
	while (true) {
		int64_t now = esp_timer_get_time();
		if (gpio_get_level(GPIO_NUM_4) == 0 && gpio_get_level(GPIO_NUM_0) == 0) {
			if (!heldSinceUs) heldSinceUs = now;
			if (now - heldSinceUs >= 1500000) returnToKeira();
		} else {
			heldSinceUs = 0;
		}
		vTaskDelay(pdMS_TO_TICKS(25));
	}
}

static StaticTask_t s_exitChordTaskBuffer;
static StackType_t s_exitChordTaskStack[3072];

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
	ESP_LOGI(TAG, "Initializing graphics");
	gfx->init();
	ESP_LOGI(TAG, "Graphics ready; initializing mixer");
	_mixerManager = new EspMixerManager(44100, 2048);
	_mixerManager->init();
	ESP_LOGI(TAG, "Mixer ready; initializing backend");

	ConfMan.registerDefault("extrapath", Common::Path("/sd/scummvm/data/engine-data/"));
	ConfMan.registerDefault("iconspath", Common::Path("/sd/scummvm/icons/"));
	ConfMan.registerDefault("pluginspath", Common::Path("/sd/scummvm/plugins/"));
	ConfMan.registerDefault("savepath", Common::Path("/sd/scummvm/saves/"));
	ConfMan.registerDefault("themepath", Common::Path("/sd/scummvm/data/themes/"));
	// Theme assets are supplied on the SD card by the user.
	ConfMan.registerDefault("gui_theme", "scummmodern");
	ConfMan.registerDefault("gui_renderer", "normal");
	ConfMan.registerDefault("browser_lastpath", "/sd/games/scummvm");

	BaseBackend::initBackend();
	ESP_LOGI(TAG, "Backend ready");
	if (s_managedLaunch)
		ConfMan.setBool("gui_return_to_launcher_at_exit", false, Common::ConfigManager::kTransientDomain);

}

// Lilka v2 buttons are active-low with pull-ups. Match the SDK's physical
// A=GPIO5/B=GPIO6 mapping; manifest actions determine left/right click.
static constexpr gpio_num_t kButtonPins[] = {
    GPIO_NUM_38, GPIO_NUM_41, GPIO_NUM_39, GPIO_NUM_40, // directions
    GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_10, GPIO_NUM_9,   // physical A/B/C/D
    GPIO_NUM_4, GPIO_NUM_0                              // Start/Select
};
enum ButtonIndex { UP, DOWN, LEFT, RIGHT, A, B, C, D, START, SELECT, BUTTON_COUNT };
static bool s_buttonDown[BUTTON_COUNT] = {};
static int64_t s_directionSinceUs = 0;

static void lilka_buttons_init() {
    for (gpio_num_t pin : kButtonPins) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }
}

bool OSystem_esp32::pollEvent(Common::Event &event) {
    ((DefaultTimerManager *)getTimerManager())->checkTimers();
    event.type = Common::EVENT_INVALID;

    const int64_t now = esp_timer_get_time();
    bool down[BUTTON_COUNT];
    for (int i = 0; i < BUTTON_COUNT; ++i)
        down[i] = gpio_get_level(kButtonPins[i]) == 0;

    for (int i = A; i < BUTTON_COUNT; ++i) {
        if (down[i] == s_buttonDown[i]) continue;
        s_buttonDown[i] = down[i];
        if ((i == START || i == SELECT) && down[START] && down[SELECT]) continue;
        LilkaButtonAction action = s_buttonActions[i - A];
        if (action == LilkaButtonAction::None) continue;
        if (action == LilkaButtonAction::LeftClick || action == LilkaButtonAction::RightClick) {
            event.type = action == LilkaButtonAction::LeftClick ?
                (down[i] ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP) :
                (down[i] ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP);
            event.mouse = _mousePos;
            return true;
        }
        if (action == LilkaButtonAction::VirtualKeyboard) {
            if (down[i]) {
                event.type = Common::EVENT_VIRTUAL_KEYBOARD;
                return true;
            }
            continue;
        }
        event.type = down[i] ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
        switch (action) {
        case LilkaButtonAction::Escape:
            event.kbd.keycode = Common::KEYCODE_ESCAPE;
            event.kbd.ascii = Common::ASCII_ESCAPE;
            break;
        case LilkaButtonAction::Space:
            event.kbd.keycode = Common::KEYCODE_SPACE;
            event.kbd.ascii = Common::ASCII_SPACE;
            break;
        case LilkaButtonAction::F5:
            event.kbd.keycode = Common::KEYCODE_F5;
            event.kbd.ascii = Common::ASCII_F5;
            break;
        case LilkaButtonAction::F7:
            event.kbd.keycode = Common::KEYCODE_F7;
            event.kbd.ascii = Common::ASCII_F7;
            break;
        default:
            event.kbd.keycode = Common::KEYCODE_RETURN;
            event.kbd.ascii = Common::ASCII_RETURN;
            break;
        }
        event.kbd.flags = 0;
        return true;
    }

    if (now - _last_input_poll_us < 16667) return false;
    _last_input_poll_us = now;
    int dx = (int)down[RIGHT] - (int)down[LEFT];
    int dy = (int)down[DOWN] - (int)down[UP];
    if (!dx && !dy) {
        s_directionSinceUs = 0;
        return false;
    }
    if (!s_directionSinceUs) s_directionSinceUs = now;
    const int step = now - s_directionSinceUs > s_pointerAccelerationUs ? s_pointerFastStep : s_pointerSlowStep;
    const int maxX = _graphicsManager->isOverlayVisible() ?
        _graphicsManager->getOverlayWidth() - 1 : _graphicsManager->getWidth() - 1;
    const int maxY = _graphicsManager->isOverlayVisible() ?
        _graphicsManager->getOverlayHeight() - 1 : _graphicsManager->getHeight() - 1;
    const int x = std::max(0, std::min(maxX, (int)_mousePos.x + dx * step));
    const int y = std::max(0, std::min(maxY, (int)_mousePos.y + dy * step));
    if (x == _mousePos.x && y == _mousePos.y) return false;
    _mousePos = Common::Point(x, y);
    _graphicsManager->warpMouse(x, y);
    event.type = Common::EVENT_MOUSEMOVE;
    event.mouse = _mousePos;
    return true;
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
	returnToKeira();
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
	s.add("engine-data", new Common::FSDirectory("/sd/scummvm/data/engine-data/", 4), priority);
	s.add("gui/themes", new Common::FSDirectory("/sd/scummvm/data/themes/", 4), priority);
}

Common::Path OSystem_esp32::getDefaultConfigFileName() {
	return "/sd/scummvm/scummvm.ini";
}

Common::Path OSystem_esp32::getDefaultLogFileName() {
	return "/sd/scummvm/scummvm.log";
}

OSystem *OSystem_esp32_create(bool silenceLogs) {
	return new OSystem_esp32(silenceLogs);
}

extern "C" {

void main_task(void *param) {
	ESP_LOGI(TAG, "Main task: reading Keira launch request");
	LilkaLaunchGame game;
	LilkaLaunchStatus launch = lilkaReadLaunchGame(game);
	ESP_LOGI(TAG, "Launch request status: %d", static_cast<int>(launch));
	if (launch == LilkaLaunchStatus::Invalid) returnToKeira();
	Common::String pathArg;
	Common::String gameArg;
	Common::String languageArg;
	Common::String platformArg;
	const char *argv[6] = {"scummvm"};
	int argc = 1;
	if (launch == LilkaLaunchStatus::Valid) {
		s_managedLaunch = true;
		for (int i = 0; i < 6; ++i) s_buttonActions[i] = game.buttons[i];
		s_pointerSlowStep = game.slowStep;
		s_pointerFastStep = game.fastStep;
		s_pointerAccelerationUs = game.accelerationMs * 1000;
		pathArg = "--path=" + game.path;
		gameArg = "--game=" + game.gameId;
		argv[argc++] = pathArg.c_str();
		argv[argc++] = gameArg.c_str();
		if (!game.language.empty()) {
			languageArg = "--language=" + game.language;
			argv[argc++] = languageArg.c_str();
		}
		if (!game.platform.empty()) {
			platformArg = "--platform=" + game.platform;
			argv[argc++] = platformArg.c_str();
		}
		argv[argc++] = "--auto-detect";
	}
	ESP_LOGI(TAG, "Entering scummvm_main");
	int res = scummvm_main(argc, argv);
	ESP_LOGW(TAG, "scummvm_main returned %d", res);
	g_system->destroy();
	returnToKeira();
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

	lilka_buttons_init();
	TaskHandle_t exitTask = xTaskCreateStaticPinnedToCore(exitChordTask, "exit_chord", 3072, nullptr, 3,
	                                                    s_exitChordTaskStack, &s_exitChordTaskBuffer, 1);
	assert(exitTask);
	sdcard_mount_blkcache("/sd", 15);
	mkdir("/sd/scummvm", 0777);
	mkdir("/sd/scummvm/saves", 0777);

	g_system = OSystem_esp32_create(false);
	assert(g_system);

	xTaskCreateStaticPinnedToCore(main_task, "main", stack_depth, NULL, 2,
	                              (StackType_t *)stackbuf, taskbuf, 0);
	return 0;
}

} //extern c
