#include "plugins/PluginHttpOrigin.h"
#include "plugins/PluginStateStore.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);}
int main() try {
    const auto manifest=[](std::string network,int version=2){return ParseManifest("{\"manifestVersion\":"+std::to_string(version)+R"(,"id":"com.example.http","name":"HTTP","version":"1.0.0","apiVersion":1,"runtime":{"kind":"native-dll","entry":"plugin.dll"},"permissions":["network.http"])"+network+"}");};
    Check(manifest("").state==PluginState::InvalidManifest,"network origins required for V2");
    const auto valid=manifest(R"(,"network":{"origins":["HTTPS://API.EXAMPLE.COM:443","https://api.example.com","https://example.org:8443"]})");
    Check(valid.manifest&&valid.manifest->networkOrigins==std::vector<std::string>{"https://api.example.com","https://example.org:8443"},"canonical sorted unique origins");
    for(const auto bad:{"http://api.example.com","https://*.example.com","https://user@api.example.com","https://api.example.com/","https://api.example.com?x","https://api.example.com#x","https://api.example.com:0","https://api.example.com:65536","https://api.example.com.","https://localhost","https://localhost.localdomain","https://foo.local","https://127.0.0.1","https://[::1]","https://0x7f000001","https://2130706433","https://api%2eexample.com","https://a\\example.com","https://a..example.com"})Check(!CanonicalHttpOrigin(bad),"unsafe origin rejected");
    Check(ParseHttpUrl("https://api.example.com/v1?q=a%20b")->origin=="https://api.example.com","path does not alter origin");
    Check(ParseHttpUrl("https://api.example.com.evil.test/")->origin!="https://api.example.com","deceptive suffix not authorized");
    Check(ParseHttpUrl("https://api.example.com:8443/")->origin!="https://api.example.com","effective port exact");
    for(const auto bad:{R"(,"network":false)",R"(,"network":{"origins":[]})",R"(,"network":{"origins":[1]})"})Check(manifest(bad).state==PluginState::InvalidManifest,"network types validated");
    std::string origins=",\"network\":{\"origins\":[";for(int i=0;i<33;++i){if(i)origins+=',';origins+=noven::raid::json::Quote("https://api"+std::to_string(i)+".example.com");}origins+="]}";
    Check(manifest(origins).state==PluginState::InvalidManifest,"origin count bounded");
    Check(!CanonicalHttpOrigin("https://"+std::string(513,'x')),"origin bytes bounded");
    const auto v1=manifest(R"(,"network":{"origins":["file://evil"]})",1);Check(v1.manifest&&v1.manifest->networkOrigins.empty()&&!v1.manifest->runtime,"V1 network/runtime unknown fields inert");
    PluginStateStore store;auto current=*valid.manifest;Check(store.Consent(current)&&store.Authorized(current),"explicit origin consent");
    const auto saved=PluginStateStore::Decode(store.Encode());Check(!saved.Corrupt()&&saved.Authorized(current),"origin grants persist");
    current.networkOrigins.push_back("https://new.example.com");Check(!store.Authorized(current),"new origin invalidates old consent");current.networkOrigins={"https://api.example.com"};Check(store.Authorized(current),"removal never expands privileges");
    current.id="com.example.other";Check(!store.Authorized(current),"cannot borrow another origin grant");
    Check(!SupportedPermission("filesystem.arbitrary")&&!SupportedPermission("raid.active.read"),"only managed network activated");
    std::cout<<"HTTP origin/manifest/consent contract PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
