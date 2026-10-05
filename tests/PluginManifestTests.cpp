#include "plugins/PluginManifest.h"
#include "raid/RaidJson.h"
#include <cstdlib>
#include <iostream>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
std::string Manifest(std::string_view id="com.example.loot-route",std::string_view version="1.0.0",std::string extra={}) {
    return "{\"manifestVersion\":1,\"id\":"+noven::raid::json::Quote(id)+",\"name\":\"Loot Route\",\"version\":"+noven::raid::json::Quote(version)+",\"apiVersion\":1"+extra+"}";
}
int main(){
    Check(ParseManifest(Manifest()).state==PluginState::Valid,"minimal v1");
    const auto full=ParseManifest(Manifest("dev.lingyuna.test","1.2.3-beta.1+build.05",",\"author\":\"Author\",\"description\":\"desc\\nmore\",\"homepage\":\"https://example.test/\",\"source\":\"http://example.test/source\",\"license\":\"MIT\",\"permissions\":[\"ui.page.register\",\"future.ability.read\",\"ui.page.register\"],\"unknown\":{\"number\":1.5}"));
    Check(full.state==PluginState::Valid&&full.manifest->requestedPermissions.size()==2&&full.manifest->author=="Author","full metadata and unknown decimal fields");
    Check(ParseManifest(R"({"name":"x","apiVersion":1,"version":"0.0.0","id":"a.b","manifestVersion":1})").state==PluginState::Valid,"arbitrary key ordering");
    for(const auto bad:{"", "Com.example", "com. example", "com..example", "com/example", "com.\\example", "com.example..bad", "builtin.test", "plugin.test", ".com.test", "com.test.","com.-test","com.test-","single"})Check(!ValidPluginId(bad),"strict ID syntax");
    Check(!ValidPluginId(std::string(129,'a')+".b"),"ID length");
    for(const auto good:{"1.2.3","0.0.0","1.2.3-beta.1","1.2.3+build.05","1.2.3-beta+sha-ABC"})Check(ValidSemanticVersion(good),"SemVer variants");
    for(const auto bad:{"1.2","1.2.3.4","01.2.3","v1.2.3","1.2.3-01","1.2.3-","1.2.3+","1.2.3+a..b","1.2.3+a+b","-1.2.3"})Check(!ValidSemanticVersion(bad),"invalid SemVer");
    for(const auto bad:{"{}",R"({"manifestVersion":"1"})",R"({"manifestVersion":0})",R"({"manifestVersion":-1})",R"({"manifestVersion":1,"id":2})",R"({"manifestVersion":1,"id":"a.b","name":"x","version":"1.2.3"})","[]","null","{", "{\"a\":1,\"a\":2}"})Check(ParseManifest(bad).state==PluginState::InvalidManifest,"missing/wrong/malformed required facts");
    Check(ParseManifest(R"({"manifestVersion":2})").state==PluginState::IncompatibleManifest,"future schema not reinterpreted");
    auto api=Manifest();api.replace(api.find("\"apiVersion\":1"),14,"\"apiVersion\":2");
    const auto incompatible=ParseManifest(api);Check(incompatible.state==PluginState::IncompatibleApi&&incompatible.manifest.has_value(),"unsupported API preserves metadata");
    Check(ParseManifest(Manifest("INVALID.id")).state==PluginState::InvalidManifest,"ID rejects at schema boundary");
    Check(ParseManifest(Manifest("a.b","1.2")).state==PluginState::InvalidManifest,"version rejects at schema boundary");
    for(const auto field:{"name","description","author","homepage","source","license"}) {
        auto text=Manifest();if(std::string_view(field)=="name")text.erase(text.find(",\"name\""),20);
        text.pop_back();text+=",\""+std::string(field)+"\":\""+std::string(5000,'x')+"\"}";
        Check(ParseManifest(text).state==PluginState::InvalidManifest,"bounded optional/required text");
    }
    Check(ParseManifest(Manifest("a.b","1.0.0",",\"homepage\":\"file:///outside\"")).state==PluginState::InvalidManifest,"URLs presentation http/https only");
    Check(ParseManifest(Manifest("a.b","1.0.0",",\"permissions\":[\"Bad.permission\"]")).state==PluginState::InvalidManifest,"permission syntax");
    std::string permissions=",\"permissions\":[";for(int i=0;i<65;++i){if(i)permissions+=',';permissions+="\"future.test\"";}permissions+=']';
    Check(ParseManifest(Manifest("a.b","1.0.0",permissions)).state==PluginState::InvalidManifest,"permission count");
    Check(ParseManifest(Manifest("a.b","1.0.0",",\"permissions\":[\""+std::string(129,'a')+".b\"]")).state==PluginState::InvalidManifest,"permission length");
    Check(ParseManifest(std::string(MaximumManifestBytes+1,' ')).state==PluginState::InvalidManifest,"manifest capacity");
    auto utf8=Manifest();utf8.insert(utf8.find("Loot"),1,static_cast<char>(0xff));Check(ParseManifest(utf8).state==PluginState::InvalidManifest,"invalid UTF-8");
    Check(ParseManifest(Manifest("a.b","1.0.0",",\"description\":\"\\uD800\"")).state==PluginState::InvalidManifest,"unpaired surrogate");
    Check(ParseManifest(Manifest("a.b","1.0.0",",\"entry\":\"evil.exe\",\"command\":\"ignored\"")).state==PluginState::Valid,"unknown executable-looking data creates no entry contract");
    std::cout<<"Manifest v1 validation PASS\n";
}
