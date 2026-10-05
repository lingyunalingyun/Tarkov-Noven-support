#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace noven::plugins {
inline constexpr std::size_t MaximumDocumentBytes=24*1024,MaximumPageBlocks=64,MaximumPluginPages=8;
enum class BlockType { Heading,Text,KeyValue,Badge,Separator,Button };
struct UiBlock final {BlockType type{};std::string text,value,action;bool operator==(const UiBlock&) const=default;};
struct UiDocument final {
    std::vector<UiBlock> blocks;
    bool HasAction(std::string_view id) const;
    bool operator==(const UiDocument&) const=default;
};
bool ValidLocalId(std::string_view value);
bool ValidUiText(std::string_view value,std::size_t maximum,bool multiline=false);
// 文档限 24 KiB；传输二次转义后仍单独检查 64 KiB 帧上限，不允许自定义绘制数据。
// Documents are capped at 24 KiB; escaped transport is separately capped at 64 KiB, with no custom drawing data.
UiDocument ParseUiDocument(std::string_view json);
}
