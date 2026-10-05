#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace noven::plugins::ipc {
// 传输版本独立于清单与未来 API；这里没有任意调用、UI 指针或执行消息。
// Transport version is separate from manifest/API versions; no arbitrary calls, UI pointers or execution messages.
inline constexpr std::int64_t TransportProtocolVersion=1;
inline constexpr std::size_t MaximumFrameBytes=64*1024;
enum class MessageType { Hello,HelloAck,Ping,Pong,Shutdown,ShutdownAck,ProtocolError };
struct Message final {
    MessageType type{MessageType::Ping};
    std::int64_t protocolVersion{TransportProtocolVersion};
    std::string pluginId,session;
};
bool ValidSecret(std::string_view value);
Message ParseMessage(std::string_view payload);
std::string Serialize(const Message& message);
bool MatchesSession(const Message& message,std::string_view pluginId,std::string_view secret);
std::vector<std::uint8_t> Frame(std::string_view payload);
// 每次回调消费一个帧，不积累无界输出队列；错误后解码器不可复用。
// Consume one frame per callback without an unbounded output queue; errors poison the decoder.
class FrameDecoder final {
public:
    void Feed(std::span<const std::uint8_t> bytes,const std::function<void(std::string_view)>& consume);
    bool Complete() const {return !failed_&&headerBytes_==0&&payload_.empty();}
private:
    std::uint32_t length_{};
    unsigned headerBytes_{};
    std::string payload_;
    bool failed_{};
};
}
