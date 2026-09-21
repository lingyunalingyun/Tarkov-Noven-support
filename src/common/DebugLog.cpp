#include "common/DebugLog.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace noven::common {

void DebugLog(std::wstring_view message) {
    static std::mutex log_mutex;
    std::lock_guard lock(log_mutex);

    std::wstring line(message);
    line.push_back(L'\n');
    OutputDebugStringW(line.c_str());

    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, ARRAYSIZE(module_path));
    if (length == 0 || length == ARRAYSIZE(module_path)) {
        return;
    }

    const std::filesystem::path log_path =
        std::filesystem::path(std::wstring(module_path, length)).parent_path()
        / L"debug-captures"
        / L"capture.log";
    std::error_code directory_error;
    std::filesystem::create_directories(log_path.parent_path(), directory_error);
    if (directory_error) {
        return;
    }

    std::wofstream log(log_path, std::ios::app);
    if (log) {
        log << line;
    }
}

} // namespace noven::common
