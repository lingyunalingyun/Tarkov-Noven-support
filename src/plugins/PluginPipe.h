#pragma once
#include "plugins/PluginProtocol.h"
#include <windows.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <utility>

namespace noven::plugins::ipc {
class Handle final {
public:
    explicit Handle(HANDLE value=nullptr):value_(value){}
    ~Handle(){Reset();}
    Handle(Handle&& other) noexcept:value_(std::exchange(other.value_,nullptr)){}
    Handle& operator=(Handle&& other) noexcept {if(this!=&other){Reset();value_=std::exchange(other.value_,nullptr);}return *this;}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    HANDLE Get() const {return value_;}
    explicit operator bool() const {return value_&&value_!=INVALID_HANDLE_VALUE;}
    void Reset(){if(*this)CloseHandle(value_);value_=nullptr;}
private:HANDLE value_{};
};
using Deadline=std::chrono::steady_clock::time_point;
Deadline After(DWORD milliseconds);
enum class IoResult { Complete,Interrupted,Timeout,Disconnected };
std::string RandomSecret();
std::wstring PipeName(std::string_view secret);
std::wstring QuoteArgument(std::wstring_view value);
Handle CreateServer(std::wstring_view pipeName);
IoResult ConnectServer(HANDLE pipe,Deadline deadline,HANDLE interrupt=nullptr,HANDLE peer=nullptr);
Handle ConnectClient(std::wstring_view name,DWORD expectedParent);
Handle CreateHostJob();
struct HostProcess final {Handle process;DWORD id{};};
HostProcess LaunchHost(const std::filesystem::path& executable,HANDLE job,std::wstring_view pipe,std::string_view pluginId,std::string_view secret);
struct HostArguments final {std::wstring pipe;std::string pluginId,secret;DWORD parent{};};
HostArguments ParseHostArguments(int count,wchar_t** arguments);
// 通道保留被打断读取的进度；控制事件不会丢失半帧。只在会话工作线程使用。
// Preserve interrupted read progress, never losing partial frames; use only on the session worker.
class Channel final {
public:
    explicit Channel(HANDLE pipe):pipe_(pipe){}
    IoResult Read(Message& message,Deadline deadline,HANDLE interrupt=nullptr,HANDLE peer=nullptr);
    IoResult Write(const Message& message,Deadline deadline,HANDLE peer=nullptr);
private:
    HANDLE pipe_{};
    std::array<std::uint8_t,4> header_{};
    std::size_t headerOffset_{},payloadOffset_{};
    std::string payload_;
    Deadline frameDeadline_{Deadline::max()};
};
struct HostCallbacks final {
    std::function<bool(const Message&,Channel&,HANDLE)> message;
    std::function<void()> shutdown;
    std::function<HANDLE()> wake;
    std::function<bool()> pump;
};
int RunHost(const HostArguments& arguments,const HostCallbacks& callbacks={});
}
