#pragma once

// 使用 Win32 全局热键消息触发单次扫描，不在空闲时轮询按键或屏幕。
// Use Win32 global-hotkey messages for single-shot scans; no idle key or screen polling.

#include <windows.h>

namespace noven::hotkey {

struct HotkeyDefinition final {
    UINT modifiers{};
    UINT virtual_key{};
};

class GlobalHotkey final {
public:
    GlobalHotkey() = default;
    ~GlobalHotkey();

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    bool Register(HWND target_window, int id, HotkeyDefinition definition);
    void Unregister();

private:
    HWND target_window_{};
    int id_{};
};

} // namespace noven::hotkey
