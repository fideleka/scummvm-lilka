#ifndef LILKA_LAUNCH_MANIFEST_H
#define LILKA_LAUNCH_MANIFEST_H

#include "common/str.h"

enum class LilkaLaunchStatus { NoRequest, Valid, Invalid };
enum class LilkaButtonAction { LeftClick, RightClick, Enter, Escape, Space, F5, F7, VirtualKeyboard, None };

struct LilkaLaunchGame {
	Common::String path;
	Common::String gameId;
	Common::String language;
	Common::String platform;
	LilkaButtonAction buttons[6] = {
		LilkaButtonAction::LeftClick, LilkaButtonAction::RightClick, LilkaButtonAction::F5,
		LilkaButtonAction::F7, LilkaButtonAction::Enter, LilkaButtonAction::VirtualKeyboard};
	int slowStep = 1;
	int fastStep = 4;
	int accelerationMs = 500;
};

// Reads Keira's CRC-protected RTC command, invalidates it, then validates the
// manifest again from SD. A raw .bin launch has no request and uses the GUI.
LilkaLaunchStatus lilkaReadLaunchGame(LilkaLaunchGame &game);

#endif
