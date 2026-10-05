#include "plugins/PluginUiDocument.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok){if(!ok)throw std::runtime_error("plugin document assertion");}
void Bad(std::string_view json){bool rejected=false;try{ParseUiDocument(json);}catch(const std::exception&){rejected=true;}Check(rejected);}
int main() try {
    auto document=ParseUiDocument(R"({"schemaVersion":1,"blocks":[{"type":"heading","text":"Hello"},{"type":"text","text":"line\nmore"},{"type":"keyValue","key":"Count","value":"0"},{"type":"badge","text":"Local"},{"type":"separator"},{"type":"button","id":"refresh","label":"Refresh"}]})");
    Check(document.blocks.size()==6&&document.HasAction("refresh")&&!document.HasAction("other"));
    Check(ValidLocalId("dashboard")&&ValidLocalId(std::string(64,'a'))&&!ValidLocalId(std::string(65,'a')));
    for(const auto id:{"", "builtin.plugins","other.page","a/b","a\\b","UPPER","-bad","bad-","a b"})Check(!ValidLocalId(id));
    for(const auto json:{"{",R"({"schemaVersion":2,"blocks":[]})",R"({"schemaVersion":1,"blocks":[{"type":"html","text":"x"}]})",R"({"schemaVersion":1,"blocks":[{"type":"button","id":"builtin.settings","label":"x"}]})",R"({"schemaVersion":1,"blocks":[{"type":"text","text":"x","color":"red"}]})",R"({"schemaVersion":1,"blocks":[{"type":"separator","target":"builtin.map"}]})",R"({"schemaVersion":1,"blocks":[{"type":"button","id":"x","label":"x"},{"type":"button","id":"x","label":"x"}]})"})Bad(json);
    Bad(std::string(MaximumDocumentBytes+1,' '));
    auto utf8=std::string(R"({"schemaVersion":1,"blocks":[{"type":"text","text":")")+static_cast<char>(0xff)+R"("}]})";Bad(utf8);
    std::string blocks="{\"schemaVersion\":1,\"blocks\":[";for(int i=0;i<64;++i){if(i)blocks+=',';blocks+="{\"type\":\"separator\"}";}Check(ParseUiDocument(blocks+"]}").blocks.size()==64);Bad(blocks+",{\"type\":\"separator\"}]}");
    Check(ValidUiText(std::string(4096,'x'),4096,true)&&!ValidUiText(std::string(4097,'x'),4096,true)&&!ValidUiText("\n",256));
    std::cout<<"Plugin UI document PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
