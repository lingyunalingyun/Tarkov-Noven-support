#pragma once
#include "events/EventHttp.h"
#include "events/EventTypes.h"
#include <filesystem>
#include <stop_token>

namespace noven::events {
// 仅生命周期/单次 worker 使用；UI 只读取已装饰快照，不访问网络或此可变缓存。
// Lifecycle/single-shot worker only; UI reads decorated snapshots, never this mutable cache or network.
class EventTranslation final {
public:
    explicit EventTranslation(IEventHttp& http):http_(http){}
    ~EventTranslation();
    bool Load(const std::filesystem::path& file,std::string& error);
    bool Refresh(const std::vector<EventRecord>& records,std::stop_token stop,std::string& error);
    void Apply(std::vector<EventRecord>& records) const;
    static std::vector<std::string> Split(std::string_view text);
    static bool Parse(const HttpResponse&,std::string& translated,std::string& error);
private:
    bool Save(std::string& error);
    IEventHttp& http_;
    std::filesystem::path file_;
    void* lease_{reinterpret_cast<void*>(-1)};
    bool writable_{};
    std::map<std::string,std::string> text_;
};
}
