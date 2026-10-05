#include "plugins/PluginProtocol.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins::ipc;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
template<class F> void Reject(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,"protocol must reject invalid input");}
int main(){
    Message hello{MessageType::Hello,1,"com.example.loot-route",std::string(64,'a')};
    const auto encoded=Serialize(hello);Check(MatchesSession(ParseMessage(encoded),hello.pluginId,hello.session),"handshake round trip");
    for(auto bad: {Message{MessageType::Hello,2,hello.pluginId,hello.session},Message{MessageType::Hello,1,"com.other.test",hello.session},Message{MessageType::Hello,1,hello.pluginId,std::string(64,'b')}})
        Check(!MatchesSession(bad,hello.pluginId,hello.session),"all handshake facts checked");
    for(const auto type:{MessageType::HelloAck,MessageType::Ping,MessageType::Pong,MessageType::Shutdown,MessageType::ShutdownAck,MessageType::ProtocolError}){
        auto message=hello;message.type=type;Check(ParseMessage(Serialize(message)).type==type,"all tiny protocol messages");
    }
    const auto frame=Frame(encoded);
    for(std::size_t split=0;split<=frame.size();++split){
        FrameDecoder decoder;unsigned count=0;auto consume=[&](std::string_view text){Check(text==encoded,"partial reads preserve bytes");++count;};
        decoder.Feed(std::span(frame).first(split),consume);decoder.Feed(std::span(frame).subspan(split),consume);
        Check(count==1&&decoder.Complete(),"every header/payload split");
    }
    auto two=frame;two.insert(two.end(),frame.begin(),frame.end());FrameDecoder decoder;unsigned count=0;
    decoder.Feed(two,[&](std::string_view text){ParseMessage(text);++count;});Check(count==2&&decoder.Complete(),"coalesced frames");
    FrameDecoder partial;partial.Feed(std::span(frame).first(frame.size()-1),[](auto){});Check(!partial.Complete(),"disconnect mid-frame rejects completeness");
    for(const auto bytes:{std::vector<std::uint8_t>{0,0,0,0},std::vector<std::uint8_t>{1,0,1,0}}){FrameDecoder bad;Reject([&]{bad.Feed(bytes,[](auto){});});Check(!bad.Complete(),"invalid decoder remains poisoned");}
    Reject([]{Frame("");});Reject([]{Frame(std::string(MaximumFrameBytes+1,'x'));});
    Check(Frame(std::string(MaximumFrameBytes,'x')).size()==MaximumFrameBytes+4,"exact frame capacity");
    for(const auto bad:{"{","[]",R"({"type":"unknown"})",R"({"type":"hello"})",R"({"type":1})",R"({"type":"ping","method":"execute"})",R"({"type":"hello","protocolVersion":"1","pluginId":"a.b","session":"x"})"})Reject([&]{ParseMessage(bad);});
    Reject([]{ParseMessage(std::string("{\"type\":\"ping")+static_cast<char>(0xff)+"\"}");});
    Check(!ValidSecret(std::string(64,'z'))&&!ValidSecret("secret"),"strict token format");
    std::cout<<"Bounded plugin protocol PASS\n";
}
