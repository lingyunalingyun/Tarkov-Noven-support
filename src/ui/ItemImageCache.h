#pragma once

#include <windows.h>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace noven::ui {

struct ItemImage final {
    std::string id;
    UINT width{};
    UINT height{};
    // 工作线程传递 CPU 像素；目标相关的 Direct2D 位图由 UI 线程创建。
    // The worker transfers CPU pixels; the UI thread creates target-dependent Direct2D bitmaps.
    std::vector<unsigned char> pixels;
};

// 下载、磁盘与 WIC 解码只在工作线程执行；UI 消息仅通知领取像素。
// Download, disk I/O and WIC decoding stay on the worker; the UI message signals ready pixels.
class ItemImageCache final {
public:
    static constexpr UINT kReadyMessage = WM_APP + 3;
    ~ItemImageCache();
    // 窗口拥有此服务：启动一次，销毁窗口前停止并等待工作线程退出。
    // The window owns this service: start once, then stop and join before destroying the window.
    void Start(HWND window, std::filesystem::path directory);
    void Stop();
    void Request(const std::string& id);
    void Forget(const std::string& id);
    [[nodiscard]] std::vector<ItemImage> TakeReady();
    [[nodiscard]] static bool ValidId(const std::string& id) noexcept;
    // 调用线程须初始化 COM；输出为预乘 BGRA，适合 Direct2D。
    // The calling thread must initialize COM; output is premultiplied BGRA for Direct2D.
    [[nodiscard]] static bool Decode(const std::vector<unsigned char>& bytes, ItemImage& image);

private:
    void Run();
    HWND window_{};
    std::filesystem::path directory_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::string> pending_;
    std::unordered_set<std::string> requested_;
    std::vector<ItemImage> ready_;
    bool stopping_{};
    std::thread worker_;
};

} // namespace noven::ui
