#include "plugins/PluginUiDocument.h"
#include "raid/RaidJson.h"
#include <algorithm>
#include <array>
#include <set>

namespace noven::plugins {
bool ValidLocalId(std::string_view value) {
    return !value.empty()&&value.size()<=64&&value.front()!='-'&&value.back()!='-'
        &&value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==value.npos;
}
bool ValidUiText(std::string_view value,std::size_t maximum,bool multiline) {
    return !value.empty()&&value.size()<=maximum
        &&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0)
        &&std::none_of(value.begin(),value.end(),[&](unsigned char c){return c==127||(c<32&&!(multiline&&(c=='\n'||c=='\r'||c=='\t')));});
}
bool UiDocument::HasAction(std::string_view id) const {
    return std::any_of(blocks.begin(),blocks.end(),[&](const auto& b){return b.type==BlockType::Button&&b.action==id;});
}
UiDocument ParseUiDocument(std::string_view json) {
    if(json.empty()||json.size()>MaximumDocumentBytes)throw std::runtime_error("invalid plugin document");
    const auto root=raid::json::Parser(json).Parse();
    if(root.At("schemaVersion").Int()!=1||root.object.size()!=2)throw std::runtime_error("invalid plugin document schema");
    const auto& blocks=root.At("blocks").Array();if(blocks.size()>MaximumPageBlocks)throw std::runtime_error("plugin document block limit");
    constexpr std::array<std::string_view,6> names{"heading","text","keyValue","badge","separator","button"};
    UiDocument document;std::set<std::string> actions;
    for(const auto& object:blocks) {
        const auto& type=object.At("type").String();const auto found=std::find(names.begin(),names.end(),type);
        if(found==names.end())throw std::runtime_error("unknown plugin block");
        UiBlock block;block.type=static_cast<BlockType>(found-names.begin());std::size_t count=1;
        if(block.type!=BlockType::Separator) {
            block.text=object.At(block.type==BlockType::Button?"label":block.type==BlockType::KeyValue?"key":"text").String();++count;
            if(!ValidUiText(block.text,block.type==BlockType::Text?4096:256,block.type==BlockType::Text))throw std::runtime_error("invalid plugin block text");
        }
        if(block.type==BlockType::KeyValue){block.value=object.At("value").String();++count;if(!ValidUiText(block.value,1024))throw std::runtime_error("invalid plugin value");}
        if(block.type==BlockType::Button){block.action=object.At("id").String();++count;if(!ValidLocalId(block.action)||!actions.insert(block.action).second)throw std::runtime_error("invalid plugin action");}
        if(object.object.size()!=count)throw std::runtime_error("unknown plugin block field");
        document.blocks.push_back(std::move(block));
    }
    return document;
}
}
