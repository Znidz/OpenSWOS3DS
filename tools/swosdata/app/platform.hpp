// The few things OpenSWOS Setup needs from the operating system, kept out of
// main.cpp so windows.h never meets the generated Slint header.
//
// Every string is UTF-8.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sd {

// The folder the executable is in ("" if it cannot be found).
std::string ExeDir();

// The parent of a path, or "" at the top.
std::string ParentDir(const std::string& path);

bool IsFile(const std::string& path);
bool IsDir(const std::string& path);

// Open a URL in the browser.
bool OpenUrl(const std::string& url);

// Start a program, detached, in `cwd`. False when it could not be started.
bool Launch(const std::string& exe, const std::string& cwd);

// Windows: set the title-bar / taskbar icon from the .exe's icon resource at
// the sizes Windows asks for (Slint hands Windows one bitmap for both). False
// while the window does not exist yet; true elsewhere.
bool ApplyNativeWindowIcon();

// Windows: a GUI-subsystem program has no console; attach the parent's so
// --help prints somewhere. No-op elsewhere.
void AttachParentConsole();

// The player's Downloads folder ("" when there is none).
std::string DownloadsDir();

// What is in a folder, not inside its folders. Hidden entries are left out.
struct DirEntry {
  std::string path;
  bool dir;
  uint64_t size;
  int64_t mtime;
};
std::vector<DirEntry> ListDir(const std::string& dir);

// Folders the PC game may have been installed to by GOG: Windows' GOG registry
// entries whose name has "Sensible" in it, and the GOG Games folders.
std::vector<std::string> GogInstallDirs();

// Set an environment variable for this process, unless it is set already.
void SetEnvDefault(const char* name, const char* value);

// The setup's log, `path`, written afresh (its folder made if need be). On Windows, a
// program started from Explorer has no stderr; then stderr goes to the log too, so what
// a library prints before it gives up is kept. NULL when it cannot be opened.
FILE* OpenLog(const std::string& path);

}  // namespace sd
