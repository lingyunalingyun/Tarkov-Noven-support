#pragma once
#include "plugins/PluginPipe.h"

namespace noven::plugins {
// 句柄存活期间禁止替换目录/文件及写 DLL，父进程与 Host 都须独立验证。
// Retained handles deny replacement/writes; parent and Host independently validate before execution.
struct NativeFile final {
    ipc::Handle root,directory,file;
    std::filesystem::path path;
    static NativeFile Open(const std::filesystem::path& root,const std::filesystem::path& directory,std::string_view entry);
};
}
