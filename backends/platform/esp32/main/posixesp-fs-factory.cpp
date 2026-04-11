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

// This is custom to ESP-IDF as it doesn't show the directories in /. As such,
// this hacks in /sdcard as the root / cwd. It also subclasses
// POSIXFilesystemNode so that exists()/isReadable()/isWritable() use stat()
// instead of access(): the joltwallet/littlefs VFS does not implement the
// access() syscall, so the default POSIXFilesystemNode incorrectly reports
// every path as non-existent on a LittleFS-backed mount.

// Re-enable some forbidden symbols to avoid clashes with stat.h and unistd.h.
// Also with clock() in sys/time.h in some macOS SDKs.
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_unistd_h
#define FORBIDDEN_SYMBOL_EXCEPTION_mkdir
#define FORBIDDEN_SYMBOL_EXCEPTION_exit		//Needed for IRIX's unistd.h
#define FORBIDDEN_SYMBOL_EXCEPTION_random
#define FORBIDDEN_SYMBOL_EXCEPTION_srandom

#include "posixesp-fs-factory.h"
#include "backends/fs/posix/posix-fs.h"
#include "backends/fs/abstract-fs.h"
#include "common/algorithm.h"

#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

namespace {

class POSIXESPFilesystemNode : public POSIXFilesystemNode {
public:
	explicit POSIXESPFilesystemNode(const Common::String &p) : POSIXFilesystemNode(p) {}

	// joltwallet/littlefs doesn't implement access(), so use stat() to
	// answer existence/permission queries.
	bool exists() const override {
		struct stat st;
		return stat(getPath().c_str(), &st) == 0;
	}
	bool isReadable() const override {
		struct stat st;
		return stat(getPath().c_str(), &st) == 0;
	}
	bool isWritable() const override {
		struct stat st;
		return stat(getPath().c_str(), &st) == 0;
	}

	// joltwallet/littlefs sets d_type=1 (DT_FIFO) on every entry instead
	// of DT_REG/DT_DIR, which causes the default POSIXFilesystemNode
	// implementation to drop every directory entry as invalid. Override
	// getChildren to ignore d_type entirely and stat() each entry.
	bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override {
		Common::String path = getPath();
		DIR *dirp = opendir(path.c_str());
		if (!dirp) return false;
		struct dirent *dp;
		while ((dp = readdir(dirp)) != NULL) {
			if (dp->d_name[0] == '.' && !hidden) continue;
			if (dp->d_name[0] == '.' && (dp->d_name[1] == 0 || (dp->d_name[1] == '.' && dp->d_name[2] == 0)))
				continue;

			Common::String childPath = path;
			if (childPath.empty() || childPath.lastChar() != '/') childPath += '/';
			childPath += dp->d_name;

			struct stat st;
			if (stat(childPath.c_str(), &st) != 0) continue;
			bool isDir = S_ISDIR(st.st_mode);

			if ((mode == Common::FSNode::kListFilesOnly && isDir) ||
			    (mode == Common::FSNode::kListDirectoriesOnly && !isDir))
				continue;

			list.push_back(new POSIXESPFilesystemNode(childPath));
		}
		closedir(dirp);
		return true;
	}
};

} // namespace

AbstractFSNode *POSIXESPFilesystemFactory::makeRootFileNode() const {
	return new POSIXESPFilesystemNode("/sdcard/");
}

AbstractFSNode *POSIXESPFilesystemFactory::makeCurrentDirectoryFileNode() const {
	return new POSIXESPFilesystemNode("/sdcard/");
}

AbstractFSNode *POSIXESPFilesystemFactory::makeFileNodePath(const Common::String &path) const {
	assert(!path.empty());
	return new POSIXESPFilesystemNode(path);
}
