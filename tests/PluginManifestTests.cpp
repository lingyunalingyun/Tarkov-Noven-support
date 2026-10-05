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
    Check(ParseManifest(R"({"manifestVersion":3})").state==PluginState::IncompatibleManifest,"future schema not reinterpreted");
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
    // 精确测试每个公开上限，避免只因重复字段/其他格式错误而碰巧拒绝超长值。
    // Exercise exact public limits so unrelated JSON failures cannot accidentally satisfy rejection tests.
    for(const auto& [field,limit]:std::vector<std::pair<std::string,std::size_t>>{{"name",256},{"description",4096},{"author",256},{"homepage",2048},{"source",2048},{"license",128}}) {
        for(const std::size_t extra:{0,1}) {
            std::string value(limit+extra,'x');if(field=="homepage"||field=="source")value.replace(0,8,"https://");
            auto text=Manifest();const auto member=noven::raid::json::Quote(field)+":"+noven::raid::json::Quote(value);
            if(field=="name")text.replace(text.find("\"name\":\"Loot Route\""),19,member);
            else {text.pop_back();text+=","+member+"}";}
            Check((ParseManifest(text).state==PluginState::Valid)==(extra==0),"exact text byte boundary");
        }
    }
    Check(ParseManifest(Manifest("a."+std::string(126,'x'),"1.0.0+"+std::string(122,'x'))).state==PluginState::Valid,"exact ID and version lengths");
    Check(!ValidSemanticVersion("1.0.0+"+std::string(123,'x')),"version beyond byte boundary");
    const auto permission="future."+std::string(121,'x');
    Check(ParseManifest(Manifest("a.b","1.0.0",",\"permissions\":["+noven::raid::json::Quote(permission)+"]")).state==PluginState::Valid,"exact permission length");
    std::string atCapacity=",\"permissions\":[";
    for(int i=0;i<64;++i){if(i)atCapacity+=',';atCapacity+=noven::raid::json::Quote("future.permission"+std::to_string(i));}atCapacity+=']';
    const auto capacityResult=ParseManifest(Manifest("a.b","1.0.0",atCapacity));
    Check(capacityResult.manifest&&capacityResult.manifest->requestedPermissions.size()==64,"exact unique permissions capacity");
    auto exact=Manifest();exact.resize(MaximumManifestBytes,' ');
    Check(ParseManifest(exact).state==PluginState::Valid,"exact file capacity valid JSON");exact+=' ';
    Check(ParseManifest(exact).state==PluginState::InvalidManifest,"one byte over file capacity");
    for(const auto field:{"description","author","homepage","source","license","permissions"})
        Check(ParseManifest(Manifest("a.b","1.0.0",",\""+std::string(field)+"\":false")).state==PluginState::InvalidManifest,"optional field wrong type");
    const auto v2=[](std::string runtime){auto value=Manifest();value.replace(value.find("\"manifestVersion\":1"),19,"\"manifestVersion\":2");value.pop_back();return value+runtime+"}";};
    const auto native=ParseManifest(v2(R"(,"runtime":{"kind":"native-dll","entry":"plugin.dll"})"));
    Check(native.state==PluginState::Valid&&native.manifest->runtime&&native.manifest->runtime->entry=="plugin.dll","minimal explicit V2 native descriptor");
    for(const auto extra:{R"(,"runtime":{"entry":"evil.exe","kind":"native-dll"})",R"(,"entry":"evil.dll","dll":"evil.dll","exe":"evil.exe","command":"ignored","runtime":{"kind":"native-dll","entry":"evil.dll"})",R"(,"runtime":false)"}){
        const auto metadata=ParseManifest(Manifest("a.b","1.0.0",extra));
        Check(metadata.state==PluginState::Valid&&!metadata.manifest->runtime,"V1 runtime-like fields remain permanently inert");
    }
    for(const auto runtime:{"",R"(,"runtime":false)",R"(,"runtime":{})",R"(,"runtime":{"kind":"exe","entry":"plugin.dll"})",R"(,"runtime":{"kind":"native-dll","entry":1})"})
        Check(ParseManifest(v2(runtime)).state==PluginState::InvalidManifest,"V2 requires correct runtime fields");
    for(const auto entry:{"bin/plugin.dll","..\\plugin.dll","C:\\x\\plugin.dll","%TEMP%\\plugin.dll","plugin.exe","plugin.DLL","plugin..dll","plugin:stream.dll",".dll","CON.dll","aux.dll","COM1.dll","LPT9.dll"," plugin.dll","plugin .dll","$HOME.dll"}){
        Check(!ValidRuntimeEntry(entry),"unsafe native entry rejected");
        Check(ParseManifest(v2(",\"runtime\":{\"kind\":\"native-dll\",\"entry\":"+noven::raid::json::Quote(entry)+"}")).state==PluginState::InvalidManifest,"entry enforced by schema");
    }
    Check(ValidRuntimeEntry("plugin.dll")&&ValidRuntimeEntry("hello-1.dll")&&ValidRuntimeEntry("插件.dll"),"UTF8 direct-child filenames");
    Check(ValidRuntimeEntry(std::string(124,'x')+".dll")&&!ValidRuntimeEntry(std::string(125,'x')+".dll"),"exact filename bound");
    auto futureApi=v2(R"(,"runtime":{"kind":"native-dll","entry":"plugin.dll"})");futureApi.replace(futureApi.find("\"apiVersion\":1"),14,"\"apiVersion\":2");
    Check(ParseManifest(futureApi).state==PluginState::IncompatibleApi,"V2 API incompatibility preserved");
    std::cout<<"Manifest v1/v2 validation PASS\n";
}
