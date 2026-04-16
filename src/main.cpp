#include "input_sim.hpp"
#include "window_manager.hpp"
#include <lsproxy/addon_sdk.h>
#include "imgui.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <windows.h>

// ============================================================================
// Global state
// ============================================================================

static IHost* g_host = nullptr;
static const char* ADDON_ID = "LSP-ReShade";

std::atomic<bool> g_inputPassthrough{false};
static std::atomic<bool> g_threadRunning{true};
static std::atomic<int>  g_hotkeyVk{VK_HOME};
static std::atomic<bool> g_hotkeyCtrl{false};
static std::atomic<bool> g_hotkeyAlt{false};
static std::atomic<bool> g_hotkeyShift{false};
static std::atomic<bool> g_autoClickRepress{true};
static std::atomic<bool> g_isSimulating{false};

// ============================================================================
// Host logging helper
// ============================================================================

static void HostLog(LsProxyLogLevel level, const char* fmt, ...) {
    if (!g_host) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_host->Log(level, buf);
}

// ============================================================================
// Window detection
// ============================================================================

static HWND FindLSWindow() {
    HWND found = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
            *(HWND*)lParam = hwnd;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&found);
    return found;
}

static HWND FindTargetWindow(HWND hLS) {
    if (!hLS) return nullptr;
    HWND h = GetWindow(hLS, GW_HWNDNEXT);
    while (h) {
        if (IsWindowVisible(h)) {
            DWORD pid;
            GetWindowThreadProcessId(h, &pid);
            if (pid != GetCurrentProcessId()) {
                char cls[256];
                GetClassNameA(h, cls, sizeof(cls));
                if (strcmp(cls, "Progman") != 0 &&
                    strcmp(cls, "Shell_TrayWnd") != 0 &&
                    strcmp(cls, "WorkerW") != 0) {
                    return h;
                }
            }
        }
        h = GetWindow(h, GW_HWNDNEXT);
    }
    return nullptr;
}

static bool HasValidFocus() {
    HWND hFg = GetForegroundWindow();
    if (!hFg) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hFg, &pid);
    if (pid == GetCurrentProcessId()) return true;

    HWND hLS = FindLSWindow();
    if (hLS) {
        HWND hTarget = FindTargetWindow(hLS);
        if (hFg == hTarget) return true;
    }
    return false;
}

// ============================================================================
// Hotkey check
// ============================================================================

static bool IsHotkeyPressed() {
    if (g_hotkeyVk == 0) return false;

    bool key  = (GetAsyncKeyState(g_hotkeyVk) & 0x8000) != 0;
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
    bool shft = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;

    return key &&
           ctrl == g_hotkeyCtrl.load() &&
           alt  == g_hotkeyAlt.load() &&
           shft == g_hotkeyShift.load();
}

// ============================================================================
// Auto click & repress logic
// ============================================================================

static void RunAutoClickRepress(bool newState) {
    g_isSimulating = true;

    int vk   = g_hotkeyVk.load();
    bool c   = g_hotkeyCtrl.load();
    bool a   = g_hotkeyAlt.load();
    bool s   = g_hotkeyShift.load();

    if (newState) { // Turning ON
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        InputSim::Click();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        InputSim::PressCombo(vk, c, a, s);
    } else { // Turning OFF
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        InputSim::Click();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    g_isSimulating = false;
}

// ============================================================================
// Worker thread
// ============================================================================

static void WorkerThread() {
    HostLog(LSPROXY_LOG_INFO, "Worker thread started");
    bool lastHotkeyState = false;
    int cleanupCounter = 0;

    while (g_threadRunning) {
        // Auto-disable on focus loss
        if (g_inputPassthrough && !HasValidFocus()) {
            g_inputPassthrough = false;
            HostLog(LSPROXY_LOG_INFO, "Passthrough disabled: focus lost");
            WindowMgr::RestoreAll();
        }

        // Check hotkey
        bool pressed = IsHotkeyPressed();

        if (pressed && !lastHotkeyState && !g_isSimulating) {
            if (HasValidFocus()) {
                bool newState = !g_inputPassthrough.load();
                g_inputPassthrough = newState;
                HostLog(LSPROXY_LOG_INFO, "Passthrough %s", newState ? "ON" : "OFF");

                if (!newState) {
                    WindowMgr::RestoreAll();
                }

                if (g_autoClickRepress) {
                    std::thread([newState]() { RunAutoClickRepress(newState); }).detach();
                }
            }
        }
        lastHotkeyState = pressed;

        // Active loop: process windows
        if (g_inputPassthrough) {
            EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
                DWORD pid;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
                    WindowMgr::ProcessWindow(hwnd);
                    EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
                        WindowMgr::ProcessWindow(child);
                        return TRUE;
                    }, 0);
                }
                return TRUE;
            }, 0);

            if (++cleanupCounter >= 100) {
                WindowMgr::CleanupDeadWindows();
                cleanupCounter = 0;
            }

            ClipCursor(NULL);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    HostLog(LSPROXY_LOG_INFO, "Worker thread stopped");
}

// ============================================================================
// Settings persistence via IHost
// ============================================================================

static void LoadSettings() {
    if (!g_host) return;
    g_hotkeyVk        = std::atoi(g_host->GetConfig(ADDON_ID, "hotkey_vk", "36")); // VK_HOME
    g_hotkeyCtrl      = std::string(g_host->GetConfig(ADDON_ID, "hotkey_ctrl", "0")) == "1";
    g_hotkeyAlt       = std::string(g_host->GetConfig(ADDON_ID, "hotkey_alt", "0")) == "1";
    g_hotkeyShift     = std::string(g_host->GetConfig(ADDON_ID, "hotkey_shift", "0")) == "1";
    g_autoClickRepress = std::string(g_host->GetConfig(ADDON_ID, "auto_click_repress", "1")) == "1";
}

static void SaveSettings() {
    if (!g_host) return;
    g_host->SetConfig(ADDON_ID, "hotkey_vk", std::to_string(g_hotkeyVk.load()).c_str());
    g_host->SetConfig(ADDON_ID, "hotkey_ctrl", g_hotkeyCtrl ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "hotkey_alt", g_hotkeyAlt ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "hotkey_shift", g_hotkeyShift ? "1" : "0");
    g_host->SetConfig(ADDON_ID, "auto_click_repress", g_autoClickRepress ? "1" : "0");
    g_host->SaveConfig();
}

// ============================================================================
// UI helper
// ============================================================================

static const char* GetKeyName(int vk) {
    static char buf[32];
    if (vk == 0) return "None";
    if (vk >= VK_F1 && vk <= VK_F12) { sprintf_s(buf, "F%d", vk - VK_F1 + 1); return buf; }
    switch (vk) {
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME:   return "Home";
        case VK_END:    return "End";
        case VK_PRIOR:  return "Page Up";
        case VK_NEXT:   return "Page Down";
    }
    if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    sprintf_s(buf, "Key %d", vk);
    return buf;
}

// ============================================================================
// LosslessProxy v2 Addon Exports
// ============================================================================

LSPROXY_EXPORT void AddonInitialize(IHost* host, ImGuiContext* ctx,
                                    void* allocFunc, void* freeFunc, void* userData) {
    ImGui::SetCurrentContext(ctx);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);

    g_host = host;
    LoadSettings();

    g_threadRunning = true;
    static bool threadStarted = false;
    if (!threadStarted) {
        std::thread(WorkerThread).detach();
        threadStarted = true;
    }

    HostLog(LSPROXY_LOG_INFO, "ReShade Input Passthrough initialized");
}

LSPROXY_EXPORT void AddonShutdown() {
    HostLog(LSPROXY_LOG_INFO, "Shutting down");
    g_threadRunning = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WindowMgr::RestoreAll();
    g_host = nullptr;
}

LSPROXY_EXPORT uint32_t GetAddonCapabilities() {
    return LSPROXY_CAP_HAS_SETTINGS | LSPROXY_CAP_REQUIRES_RESTART;
}

LSPROXY_EXPORT void AddonRenderSettings() {
    ImGui::TextWrapped("Allows mouse and keyboard interaction with ReShade overlays "
                       "on top of Lossless Scaling. Press the configured hotkey to toggle.");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // Status indicator
    if (g_inputPassthrough) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1));
        ImGui::Text("Status: ACTIVE");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Status: Inactive");
    }

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // Auto click & repress
    bool autoClick = g_autoClickRepress;
    if (ImGui::Checkbox("Auto Click & Repress", &autoClick)) {
        g_autoClickRepress = autoClick;
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically clicks and re-presses the hotkey\n"
                          "to properly open/close the ReShade menu.");
    }

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Text("Hotkey");
    ImGui::Dummy(ImVec2(0, 3));

    // Modifier checkboxes
    bool c = g_hotkeyCtrl, a = g_hotkeyAlt, s = g_hotkeyShift;
    bool changed = false;
    if (ImGui::Checkbox("Ctrl", &c))  { g_hotkeyCtrl = c;  changed = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Alt", &a))   { g_hotkeyAlt = a;   changed = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Shift", &s)) { g_hotkeyShift = s;  changed = true; }

    // Key selector
    int currentKey = g_hotkeyVk;
    if (ImGui::BeginCombo("Key", GetKeyName(currentKey))) {
        static const int keys[] = {
            0, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8,
            VK_F9, VK_F10, VK_F11, VK_F12, VK_INSERT, VK_DELETE,
            VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
            'A','B','C','D','E','F','G','H','I','J','K','L','M',
            'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'
        };
        for (int vk : keys) {
            if (ImGui::Selectable(GetKeyName(vk), currentKey == vk)) {
                g_hotkeyVk = vk;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    if (changed) SaveSettings();
}

LSPROXY_EXPORT const char* GetAddonName()        { return "ReShade Input Passthrough"; }
LSPROXY_EXPORT const char* GetAddonVersion()     { return "0.3.0"; }
LSPROXY_EXPORT const char* GetAddonAuthor()      { return "LosslessProxy Team"; }
LSPROXY_EXPORT const char* GetAddonDescription() {
    return "Enables mouse and keyboard interaction with ReShade overlays on top of "
           "Lossless Scaling. Toggle input passthrough with a configurable hotkey.";
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
