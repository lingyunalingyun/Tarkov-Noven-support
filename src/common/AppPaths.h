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
    // 显式评审使用相邻的测试目录，不能嵌套在程序目录中。
    // Explicit review uses a sibling test root, never a child of the program root.
    static AppPaths ResourceReview(std::filesystem::path program);
    static AppPaths Resolve(std::filesystem::path program,const std::function<std::filesystem::path()>& knownFolder);
    static AppPaths Current();
    std::filesystem::path Data() const {return userRoot/L"data";}
    std::filesystem::path Plugins() const {return userRoot/L"plugins";}
    // 官方可再生成资源与下载缓存不属于插件或用户历史，也不写入安装目录。
    // Official reproducible resources/cache are separate from plugins/history and installed program files.
    std::filesystem::path Resources() const {return userRoot/L"resources";}
    std::filesystem::path ResourceManifests() const {return Resources()/L"manifests";}
    std::filesystem::path ResourceMaps() const {return Resources()/L"maps";}
    std::filesystem::path ResourceStaging() const {return Resources()/L"staging";}
    std::filesystem::path DownloadCache() const {return userRoot/L"downloads"/L"cache";}
    std::filesystem::path Assets() const {return programRoot/L"assets";}
    std::filesystem::path Diagnostics() const {return mode==PathMode::Development?programRoot/L"debug-captures":userRoot/L"logs";}
};
std::filesystem::path ProgramDirectory();
std::filesystem::path LocalAppDataDirectory();
}
