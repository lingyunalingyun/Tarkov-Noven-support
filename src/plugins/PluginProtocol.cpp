#include "plugins/PluginProtocol.h"
#include "plugins/PluginManifest.h"
#include "plugins/PluginUiDocument.h"
#include "raid/RaidJson.h"
#include <array>
#include <stdexcept>

namespace noven::plugins::ipc {
namespace {
constexpr std::array<std::string_view,14> Names{"hello","helloAck","ping","pong","shutdown","shutdownAck","protocolError","loadPlugin","loadPluginResult","uiRegisterPage","uiPublishPage","uiAction","uiActionResult","log"};
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
    std::size_t fields=IsHandshake(message.type)?4u:1u;
    switch(message.type) {
    case MessageType::LoadPlugin:
        if(object.At("manifestVersion").Int()!=2||object.At("apiVersion").Int()!=1||object.At("abiVersion").Int()!=1)Invalid();
        message.directory=object.At("directory").String();message.entry=object.At("entry").String();message.pagePermission=object.At("pagePermission").Bool();
        if(!ValidUiText(message.directory,8192)||!ValidRuntimeEntry(message.entry))Invalid();fields=7;break;
    case MessageType::LoadPluginResult:case MessageType::UiActionResult:
        message.result=object.At("result").Int();if(message.result<0||message.result>6)Invalid();fields=2;break;
    case MessageType::UiRegisterPage:
        message.pageId=object.At("pageId").String();message.title=object.At("title").String();
        if(!ValidLocalId(message.pageId)||!ValidUiText(message.title,256))Invalid();fields=3;break;
    case MessageType::UiPublishPage:
        message.pageId=object.At("pageId").String();message.document=object.At("document").String();
        if(!ValidLocalId(message.pageId))Invalid();ParseUiDocument(message.document);fields=3;break;
    case MessageType::UiAction:
        message.pageId=object.At("pageId").String();message.actionId=object.At("actionId").String();
        if(!ValidLocalId(message.pageId)||!ValidLocalId(message.actionId))Invalid();fields=3;break;
    case MessageType::Log:
        message.text=object.At("text").String();if(!ValidUiText(message.text,1024))Invalid();fields=2;break;
    default:break;
    }
    // 旧消息的精确 schema 不变；扩展也不能携带身份伪装/全局目标/命令。
    // Old message schemas stay exact; extensions cannot carry spoofed identities/global targets/commands.
    if(object.object.size()!=fields)Invalid();
    return message;
}
std::string Serialize(const Message& message){
    const auto index=static_cast<std::size_t>(message.type);if(index>=Names.size())Invalid();
    auto text="{\"type\":"+raid::json::Quote(Names[index]);
    if(IsHandshake(message.type)){
        if(!ValidPluginId(message.pluginId)||!ValidSecret(message.session))Invalid();
        text+=",\"protocolVersion\":"+std::to_string(message.protocolVersion)+",\"pluginId\":"+raid::json::Quote(message.pluginId)+",\"session\":"+raid::json::Quote(message.session);
    }
    switch(message.type) {
    case MessageType::LoadPlugin:
        text+=",\"manifestVersion\":2,\"apiVersion\":1,\"abiVersion\":1,\"directory\":"+raid::json::Quote(message.directory)+",\"entry\":"+raid::json::Quote(message.entry)+",\"pagePermission\":"+(message.pagePermission?"true":"false");break;
    case MessageType::LoadPluginResult:case MessageType::UiActionResult:text+=",\"result\":"+std::to_string(message.result);break;
    case MessageType::UiRegisterPage:text+=",\"pageId\":"+raid::json::Quote(message.pageId)+",\"title\":"+raid::json::Quote(message.title);break;
    case MessageType::UiPublishPage:text+=",\"pageId\":"+raid::json::Quote(message.pageId)+",\"document\":"+raid::json::Quote(message.document);break;
    case MessageType::UiAction:text+=",\"pageId\":"+raid::json::Quote(message.pageId)+",\"actionId\":"+raid::json::Quote(message.actionId);break;
    case MessageType::Log:text+=",\"text\":"+raid::json::Quote(message.text);break;
    default:break;
    }
    text+='}';ParseMessage(text);return text;
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
