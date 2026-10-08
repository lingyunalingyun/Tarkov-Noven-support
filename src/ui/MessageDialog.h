#pragma once

#include <windows.h>
#include <string_view>

namespace noven::ui {
enum class MessageKind { Information, Warning, Error };

// 所有应用消息共用主题；确认默认取消，关闭或创建失败也绝不授予权限。
// App messages share one theme; confirmations default to cancel and fail closed.
[[nodiscard]] bool ShowMessageDialog(HWND owner, std::wstring_view title,
    std::wstring_view text, MessageKind kind, std::wstring_view accept,
    std::wstring_view cancel = {});
}
