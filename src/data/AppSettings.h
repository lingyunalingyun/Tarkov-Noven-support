#pragma once
#include "data/GameMode.h"
#include "raid/RaidJson.h"
#include <filesystem>
#include <fstream>
#include <optional>

namespace noven::data {
// 只保存用户选择；路径不来自注册表、进程或全盘探测。
// Persist user choices only; paths never come from registry/process/disk discovery.
struct AppSettings final {
    GameMode mode{GameMode::Pvp};
    UINT scanKey{VK_F2},scanModifiers{};
    std::filesystem::path gameDirectory;
    bool operator==(const AppSettings&) const = default;
    static bool ValidKey(UINT key) noexcept {
        return (key>=VK_F1&&key<=VK_F24)||(key>='0'&&key<='9')||(key>='A'&&key<='Z');
    }
    bool Valid() const {
        return static_cast<int>(mode)>=0&&static_cast<int>(mode)<=2&&ValidKey(scanKey)
            &&!(scanModifiers&~(MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_WIN))
            &&(gameDirectory.empty()||gameDirectory.is_absolute());
    }
    std::string Encode() const {
        const auto path=gameDirectory.u8string();
        return "{\"schemaVersion\":1,\"mode\":"+std::to_string(static_cast<int>(mode))
            +",\"scanKey\":"+std::to_string(scanKey)+",\"scanModifiers\":"+std::to_string(scanModifiers)
            +",\"gameDirectory\":"+raid::json::Quote(std::string(path.begin(),path.end()))+"}";
    }
    static std::optional<AppSettings> Decode(std::string_view text) {
        try {
            if(text.size()>65536)return {};
            const auto value=raid::json::Parser(text).Parse();
            if(value.At("schemaVersion").Int()!=1)return {};
            const auto mode=value.At("mode").Int(),key=value.At("scanKey").Int(),mods=value.At("scanModifiers").Int();
            if(mode<0||mode>2||key<0||key>255||mods<0||mods>15)return {};
            AppSettings result;result.mode=static_cast<GameMode>(mode);result.scanKey=static_cast<UINT>(key);result.scanModifiers=static_cast<UINT>(mods);
            const auto path=value.At("gameDirectory").String();
            result.gameDirectory=std::filesystem::path(std::u8string(path.begin(),path.end()));
            return result.Valid()?std::optional(result):std::nullopt;
        } catch(...) {return {};}
    }
    static std::optional<AppSettings> Load(const std::filesystem::path& path) {
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        if(ec||size>65536)return {};
        std::ifstream in(path,std::ios::binary);std::string text(static_cast<std::size_t>(size),'\0');
        if(!in.read(text.data(),static_cast<std::streamsize>(text.size())))return {};
        return Decode(text);
    }
    bool Save(const std::filesystem::path& path) const {
        if(!Valid())return false;
        std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
        auto temporary=path;temporary+=L".tmp";
        const auto text=Encode();
        const HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        DWORD written{};const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)
            &&written==text.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(ok&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
        DeleteFileW(temporary.c_str());return false;
    }
};

inline std::optional<std::filesystem::path> GameLogRoot(const std::filesystem::path& game) {
    if(game.empty()||!game.is_absolute())return {};
    // 仅检查所选目录下的已知布局；不递归搜索磁盘。
    // Check known layouts under the selected directory only; never search disks recursively.
    for(const auto& path:{game/L"build"/L"Logs",game/L"Logs"}) {
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return path;
    }
    return {};
}
}
