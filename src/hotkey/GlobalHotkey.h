#pragma once

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
