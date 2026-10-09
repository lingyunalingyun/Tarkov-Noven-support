#include "common/DebugLog.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace noven::common {
namespace {std::mutex log_mutex;std::filesystem::path log_directory;}
void ConfigureDebugLog(const std::filesystem::path& directory){std::lock_guard lock(log_mutex);log_directory=directory;}

void DebugLog(std::wstring_view message) {
    std::lock_guard lock(log_mutex);

    std::wstring line(message);
    line.push_back(L'\n');
    OutputDebugStringW(line.c_str());

    if(log_directory.empty())return;
    const auto log_path=log_directory/L"capture.log";
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
