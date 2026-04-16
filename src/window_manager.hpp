#pragma once
#include <windows.h>
#include <atomic>

// Forward declare the passthrough flag used by window manager
extern std::atomic<bool> g_inputPassthrough;

namespace WindowMgr {

// Subclass a window and modify styles for input passthrough
void ProcessWindow(HWND hwnd);

// Restore all modified windows to original state
void RestoreAll();

// Remove entries for destroyed windows
void CleanupDeadWindows();

} // namespace WindowMgr
