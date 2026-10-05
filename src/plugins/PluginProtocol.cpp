#include "plugins/PluginProtocol.h"
#include "plugins/PluginManifest.h"
#include "raid/RaidJson.h"
#include <array>
#include <stdexcept>

namespace noven::plugins::ipc {
namespace {
constexpr std::array<std::string_view,7> Names{"hello","helloAck","ping","pong","shutdown","shutdownAck","protocolError"};
[[noreturn]] void Invalid(){throw std::runtime_error("plugin protocol violation");}
bool IsHandshake(MessageType type){return type==MessageType::Hello||type==MessageType::HelloAck;}
}
bool ValidSecret(std::string_view value){
    if(value.size()!=64)return false;
    for(const char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
Message ParseMessage(std::string_view payload){
    if(payload.empty()||payload.size()>MaximumFrameBytes)Invalid();
    const auto object=raid::json::Parser(payload).Parse();
    const auto& type=object.At("type").String();
    Message message;bool known=false;
    for(std::size_t i=0;i<Names.size();++i)if(type==Names[i]){message.type=static_cast<MessageType>(i);known=true;break;}
    if(!known)Invalid();
    if(IsHandshake(message.type)){
        message.protocolVersion=object.At("protocolVersion").Int();
        message.pluginId=object.At("pluginId").String();message.session=object.At("session").String();
        if(!ValidPluginId(message.pluginId)||!ValidSecret(message.session))Invalid();
    }
    // 协议 v1 只接受定义的字段；错误信息固定，不回显会话密钥或不可信文本。
    // V1 accepts only defined fields; errors are fixed, never echoing secrets or untrusted text.
    if(object.object.size()!=(IsHandshake(message.type)?4u:1u))Invalid();
    return message;
}
std::string Serialize(const Message& message){
    const auto index=static_cast<std::size_t>(message.type);if(index>=Names.size())Invalid();
    auto text="{\"type\":"+raid::json::Quote(Names[index]);
    if(IsHandshake(message.type)){
        if(!ValidPluginId(message.pluginId)||!ValidSecret(message.session))Invalid();
        text+=",\"protocolVersion\":"+std::to_string(message.protocolVersion)+",\"pluginId\":"+raid::json::Quote(message.pluginId)+",\"session\":"+raid::json::Quote(message.session);
    }
    return text+"}";
}
bool MatchesSession(const Message& message,std::string_view pluginId,std::string_view secret){
    if(!IsHandshake(message.type)||message.protocolVersion!=TransportProtocolVersion||message.pluginId!=pluginId||message.session.size()!=secret.size())return false;
    unsigned difference=0;for(std::size_t i=0;i<secret.size();++i)difference|=static_cast<unsigned char>(message.session[i])^static_cast<unsigned char>(secret[i]);
    return difference==0&&ValidSecret(secret);
}
std::vector<std::uint8_t> Frame(std::string_view payload){
    if(payload.empty()||payload.size()>MaximumFrameBytes)Invalid();
    std::vector<std::uint8_t> bytes;bytes.reserve(payload.size()+4);
    const auto length=static_cast<std::uint32_t>(payload.size());
    for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(length>>(8*i)));
    bytes.insert(bytes.end(),payload.begin(),payload.end());return bytes;
}
void FrameDecoder::Feed(std::span<const std::uint8_t> bytes,const std::function<void(std::string_view)>& consume){
    if(failed_)Invalid();
    try{
        for(const auto byte:bytes){
            if(headerBytes_<4){
                length_|=static_cast<std::uint32_t>(byte)<<(8*headerBytes_++);
                if(headerBytes_==4){if(length_==0||length_>MaximumFrameBytes)Invalid();payload_.reserve(length_);}
            }else{
                payload_+=static_cast<char>(byte);
                if(payload_.size()==length_){consume(payload_);payload_.clear();length_=0;headerBytes_=0;}
            }
        }
    }catch(...){failed_=true;throw;}
}
}
