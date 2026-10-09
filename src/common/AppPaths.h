#pragma once
#include <filesystem>
#include <functional>
namespace noven::common {
enum class PathMode {Development,Installed,Test};
// 安装标记仅属于 install payload；测试显式注入目录，不查询或写入真实用户数据。
// The installed marker belongs only to the install payload; tests inject roots, never real user data.
struct AppPaths final {
    PathMode mode;
    std::filesystem::path programRoot,userRoot;
    static AppPaths Development(std::filesystem::path program);
    static AppPaths Installed(std::filesystem::path program,std::filesystem::path localAppData);
    static AppPaths Test(std::filesystem::path program,std::filesystem::path testRoot);
    static AppPaths Resolve(std::filesystem::path program,const std::function<std::filesystem::path()>& knownFolder);
    static AppPaths Current();
    std::filesystem::path Data() const {return userRoot/L"data";}
    std::filesystem::path Plugins() const {return userRoot/L"plugins";}
    std::filesystem::path Assets() const {return programRoot/L"assets";}
    std::filesystem::path Diagnostics() const {return mode==PathMode::Development?programRoot/L"debug-captures":userRoot/L"logs";}
};
std::filesystem::path ProgramDirectory();
std::filesystem::path LocalAppDataDirectory();
}
