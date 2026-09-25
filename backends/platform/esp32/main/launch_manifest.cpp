#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose

#include "launch_manifest.h"
#include "common/formats/json.h"

#include "esp_crc.h"
#include "esp_log.h"

namespace {

constexpr uintptr_t kCommandAddress = 0x50000000;
constexpr size_t kCommandBytes = 1024;
constexpr size_t kMaxManifestBytes = 4096;
constexpr size_t kMaxManifestPath = 512;
#ifdef LILKA_ENGINE_KYRA
constexpr char kPrefix[] = "/sd/scummvm/engines/kyra.bin manifest=";
constexpr char kEngine[] = "kyra";
#else
constexpr char kPrefix[] = "/sd/scummvm/engines/scumm.bin manifest=";
constexpr char kEngine[] = "scumm";
#endif

struct KernelParams {
	char cmd[kCommandBytes];
	uint32_t crc;
};

bool safePath(const Common::String &path, bool absolute) {
	if (path.empty() || path.size() > kMaxManifestPath || path.contains('\\')) return false;
	if (absolute && !path.hasPrefix("/sd/")) return false;
	if (!absolute && path[0] == '/') return false;
	if (path.contains("//")) return false;
	for (size_t start = absolute ? 1 : 0; start < path.size();) {
		size_t end = path.find('/', start);
		if (end == Common::String::npos) end = path.size();
		if (path.substr(start, end - start) == "..") return false;
		start = end + 1;
	}
	return true;
}

bool safeGameId(const Common::String &id) {
	if (id.empty() || id.size() > 48) return false;
	for (char c : id) {
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	}
	return true;
}

bool safeOption(const Common::String &value) {
	if (value.size() > 16) return false;
	for (char c : value) {
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	}
	return true;
}

bool parseAction(const Common::String &name, LilkaButtonAction &action) {
	struct Choice { const char *name; LilkaButtonAction action; };
	static const Choice choices[] = {
		{"leftClick", LilkaButtonAction::LeftClick}, {"rightClick", LilkaButtonAction::RightClick},
		{"enter", LilkaButtonAction::Enter}, {"escape", LilkaButtonAction::Escape},
		{"space", LilkaButtonAction::Space}, {"f5", LilkaButtonAction::F5},
		{"f7", LilkaButtonAction::F7}, {"virtualKeyboard", LilkaButtonAction::VirtualKeyboard},
		{"none", LilkaButtonAction::None}};
	for (const Choice &choice : choices) {
		if (name == choice.name) {
			action = choice.action;
			return true;
		}
	}
	return false;
}

bool parseControls(Common::JSONValue *root, LilkaLaunchGame &game) {
	Common::JSONValue *controls = root->child("controls");
	static const char *names[] = {"a", "b", "c", "d", "start", "select"};
	if (controls) {
		if (!controls->isObject()) return false;
		for (size_t i = 0; i < 6; ++i) {
			Common::JSONValue *value = controls->child(names[i]);
			if (value && (!value->isString() || !parseAction(value->asString(), game.buttons[i]))) return false;
		}
	}
	Common::JSONValue *pointer = root->child("pointer");
	if (!pointer) return true;
	if (!pointer->isObject()) return false;
	struct Setting { const char *name; int *value; int minimum; int maximum; };
	Setting settings[] = {{"slowStep", &game.slowStep, 1, 4}, {"fastStep", &game.fastStep, 1, 12},
	                      {"accelerationMs", &game.accelerationMs, 100, 2000}};
	for (const Setting &setting : settings) {
		Common::JSONValue *value = pointer->child(setting.name);
		if (!value) continue;
		if (!value->isIntegerNumber() || value->asIntegerNumber() < setting.minimum ||
		    value->asIntegerNumber() > setting.maximum) return false;
		*setting.value = static_cast<int>(value->asIntegerNumber());
	}
	return game.fastStep >= game.slowStep;
}

int base64Value(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '-') return 62;
	if (c == '_') return 63;
	return -1;
}

bool decodePath(const char *token, Common::String &path) {
	uint32_t bits = 0;
	int count = 0;
	for (const char *p = token; *p; ++p) {
		int value = base64Value(*p);
		if (value < 0) return false;
		bits = (bits << 6) | value;
		count += 6;
		if (count >= 8) {
			count -= 8;
			char c = static_cast<char>((bits >> count) & 0xff);
			if (c == '\0' || path.size() >= kMaxManifestPath) return false;
			path += c;
		}
	}
	return count < 6 && (bits & ((1u << count) - 1)) == 0 && safePath(path, true);
}

Common::String jsonString(Common::JSONValue *object, const char *key) {
	Common::JSONValue *value = object->child(key);
	return value && value->isString() ? value->asString() : Common::String();
}

bool readManifest(const Common::String &path, LilkaLaunchGame &game) {
	struct stat fileStat;
	if (stat(path.c_str(), &fileStat) != 0 || !S_ISREG(fileStat.st_mode) ||
	    fileStat.st_size <= 0 || fileStat.st_size > static_cast<off_t>(kMaxManifestBytes)) return false;
	FILE *file = fopen(path.c_str(), "rb");
	if (!file) return false;
	char json[kMaxManifestBytes + 1];
	size_t count = fread(json, 1, fileStat.st_size, file);
	fclose(file);
	if (count != static_cast<size_t>(fileStat.st_size)) return false;
	json[count] = '\0';
	Common::JSONValue *root = Common::JSON::parse(json);
	if (!root || !root->isObject()) {
		delete root;
		return false;
	}
	Common::String schema = jsonString(root, "schema");
	Common::String title = jsonString(root, "title");
	Common::String engine = jsonString(root, "engine");
	Common::String gameId = jsonString(root, "gameId");
	Common::String relativePath = jsonString(root, "path");
	Common::String language = jsonString(root, "language");
	Common::String platform = jsonString(root, "platform");
	bool controlsValid = parseControls(root, game);
	delete root;
	if (schema != "keira-scummvm-v1" || title.empty() || title.size() > 80 ||
	    engine != kEngine || !safeGameId(gameId) || !safePath(relativePath, false) ||
	    !safeOption(language) || !safeOption(platform) || !controlsValid) return false;

	size_t slash = path.findLastOf('/');
	Common::String gamePath = path.substr(0, slash);
	if (relativePath != ".") gamePath += "/" + relativePath;
	struct stat gameStat;
	if (stat(gamePath.c_str(), &gameStat) != 0 || !S_ISDIR(gameStat.st_mode)) return false;
	game.path = gamePath;
	game.gameId = gameId;
	game.language = language;
	game.platform = platform;
	return true;
}

} // namespace

LilkaLaunchStatus lilkaReadLaunchGame(LilkaLaunchGame &game) {
	KernelParams *params = reinterpret_cast<KernelParams *>(kCommandAddress);
	uint32_t crc = esp_crc32_le(0, reinterpret_cast<const uint8_t *>(params->cmd), kCommandBytes);
	if (crc != params->crc) return LilkaLaunchStatus::NoRequest;
	char command[kCommandBytes];
	memcpy(command, params->cmd, sizeof(command));
	params->crc = 0; // One-shot request, even if validation fails.
	if (!memchr(command, '\0', sizeof(command))) return LilkaLaunchStatus::Invalid;
	if (strncmp(command, kPrefix, sizeof(kPrefix) - 1) != 0) return LilkaLaunchStatus::NoRequest;
	Common::String manifestPath;
	if (!decodePath(command + sizeof(kPrefix) - 1, manifestPath) || !readManifest(manifestPath, game)) {
		ESP_LOGE("ScummLaunch", "Invalid launch manifest");
		return LilkaLaunchStatus::Invalid;
	}
	ESP_LOGI("ScummLaunch", "Launching %s from %s", game.gameId.c_str(), game.path.c_str());
	return LilkaLaunchStatus::Valid;
}
