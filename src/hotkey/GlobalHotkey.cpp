#include "hotkey/GlobalHotkey.h"

#include "common/DebugLog.h"

#include <string>

namespace noven::hotkey {

GlobalHotkey::~GlobalHotkey() {
    Unregister();
}

bool GlobalHotkey::Register(HWND target_window, int id, HotkeyDefinition definition) {
    Unregister();
    if (!RegisterHotKey(target_window, id, definition.modifiers, definition.virtual_key)) {
        common::DebugLog(
            L"[hotkey] RegisterHotKey failed for virtual key "
            + std::to_wstring(definition.virtual_key)
            + L" (Win32 error=" + std::to_wstring(GetLastError()) + L")"
        );
        return false;
    }

    target_window_ = target_window;
    id_ = id;
    common::DebugLog(L"[hotkey] registered capture key="+std::to_wstring(definition.virtual_key));
    return true;
}

void GlobalHotkey::Unregister() {
    if (target_window_ == nullptr) {
        return;
    }

    UnregisterHotKey(target_window_, id_);
    target_window_ = nullptr;
    id_ = 0;
}

} // namespace noven::hotkey
