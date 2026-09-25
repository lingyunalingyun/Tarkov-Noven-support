#pragma once

// 诊断输出只帮助关联扫描阶段，不参与识别、目录匹配或结果显示。
// Diagnostic output correlates scan stages; it never drives recognition, matching, or display.

#include <string_view>

namespace noven::common {

void DebugLog(std::wstring_view message);

} // namespace noven::common
