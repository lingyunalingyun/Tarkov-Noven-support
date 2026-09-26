#include "ui/ItemImageCache.h"
#include <objbase.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char** argv) {
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization");
    using noven::ui::ItemImageCache;
    using noven::ui::ItemImage;
    constexpr char id[] = "5b7c710788a4506dec015957";
    Require(ItemImageCache::ValidId(id) && !ItemImageCache::ValidId("../test")
        && !ItemImageCache::ValidId("invalid"), "stable ID validation rejects unsafe paths");
    ItemImage image;
    Require(!ItemImageCache::Decode({}, image)
        && !ItemImageCache::Decode({1, 2, 3}, image), "invalid images fail safely");
    // 用小型 BMP 测试 WIC 和异步磁盘缓存，测试不依赖网络或用户历史。
    // A tiny BMP exercises WIC and async disk caching without network or user history.
    const std::vector<unsigned char> bmp{
        'B','M',58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,
        1,0,0,0,1,0,0,0,1,0,24,0,0,0,0,0,4,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,0};
    Require(ItemImageCache::Decode(bmp, image) && image.width == 1 && image.height == 1
        && image.pixels.size() == 4 && image.pixels[2] == 255 && image.pixels[3] == 255,
        "WIC produces premultiplied BGRA pixels");
    const auto directory = std::filesystem::temp_directory_path()
        / (L"NovenImages-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(directory);
    { std::ofstream output(directory / (std::string(id) + ".webp"), std::ios::binary);
      output.write(reinterpret_cast<const char*>(bmp.data()), bmp.size()); }
    ItemImageCache cache;
    cache.Start(nullptr, directory);
    cache.Request(id);
    cache.Request(id);
    std::vector<ItemImage> ready;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (ready.empty() && std::chrono::steady_clock::now() < deadline) {
        ready = cache.TakeReady();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    cache.Stop();
    Require(ready.size() == 1 && ready[0].id == id && cache.TakeReady().empty(),
        "cached image loads offline and duplicate requests coalesce");
    std::filesystem::remove_all(directory);
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        Require(ItemImageCache::Decode(bytes, image), "live-source image decodes with installed WIC codec");
        std::cout << "Image decoded: " << image.width << 'x' << image.height << '\n';
    }
    CoUninitialize();
    std::cout << "Item image tests passed\n";
}
