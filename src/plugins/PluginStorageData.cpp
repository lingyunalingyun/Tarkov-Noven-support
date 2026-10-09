#include "plugins/PluginStorageData.h"
#include <windows.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace noven::plugins {
bool ValidStorageKey(std::string_view key){
    if(key.empty()||key.size()>128||key.find("..")!=key.npos||key.find_first_of("/\\:")!=key.npos)return false;
    wchar_t wide[128]{};const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,key.data(),static_cast<int>(key.size()),wide,128);
    if(!size)return false;WORD types[128]{};if(!GetStringTypeW(CT_CTYPE1,wide,size,types))return false;
    for(int i=0;i<size;++i)if(wide[i]==0||(types[i]&C1_CNTRL))return false;return true;
}
bool ValidStorageRequest(const StorageRequest& request){
    if(!request.requestId||request.requestId>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())||request.value.size()>MaximumStorageValue)return false;
    if(request.operation==StorageOperation::List)return request.key.empty()&&request.value.empty()&&request.limit>0&&request.limit<=MaximumStorageList&&request.offset<=MaximumStorageKeys;
    if(!ValidStorageKey(request.key)||request.offset||request.limit)return false;
    return request.operation==StorageOperation::Set||((request.operation==StorageOperation::Get||request.operation==StorageOperation::Delete)&&request.value.empty());
}
bool ValidStorageResult(const StorageResult& result){
    if(!result.requestId||result.requestId>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())||static_cast<unsigned>(result.status)>7
        ||result.value.size()>MaximumStorageValue||result.keys.size()>MaximumStorageList||result.total>MaximumStorageKeys||result.nextOffset>result.total)return false;
    if(result.status!=StorageStatus::Ok)return result.value.empty()&&result.keys.empty()&&!result.total&&!result.nextOffset;
    if(!result.keys.empty()&&(!result.value.empty()||result.keys.size()>result.total))return false;
    for(std::size_t i=0;i<result.keys.size();++i)if(!ValidStorageKey(result.keys[i])||(i&&result.keys[i-1]>=result.keys[i]))return false;return true;
}
std::string EncodeStorageBytes(std::string_view bytes){
    if(bytes.size()>MaximumStorageValue)throw std::runtime_error("storage value bounds");
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3){const unsigned a=static_cast<unsigned char>(bytes[i]),b=i+1<bytes.size()?static_cast<unsigned char>(bytes[i+1]):0,c=i+2<bytes.size()?static_cast<unsigned char>(bytes[i+2]):0;
        out+=alphabet[a>>2];out+=alphabet[((a&3)<<4)|(b>>4)];out+=i+1<bytes.size()?alphabet[((b&15)<<2)|(c>>6)]:'=';out+=i+2<bytes.size()?alphabet[c&63]:'=';
    }return out;
}
std::string DecodeStorageBytes(std::string_view text){
    if(text.size()>(MaximumStorageValue+2)/3*4||text.size()%4)throw std::runtime_error("storage encoding bounds");
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;
    for(std::size_t i=0;i<text.size();i+=4){unsigned v[4]{};for(unsigned j=0;j<4;++j){const auto at=alphabet.find(text[i+j]);if(at==alphabet.npos&&text[i+j]!='=')throw std::runtime_error("storage encoding");v[j]=at==alphabet.npos?0:static_cast<unsigned>(at);}
        out+=static_cast<char>((v[0]<<2)|(v[1]>>4));if(text[i+2]!='=')out+=static_cast<char>((v[1]<<4)|(v[2]>>2));if(text[i+3]!='=')out+=static_cast<char>((v[2]<<6)|v[3]);
    }
    if(out.size()>MaximumStorageValue||EncodeStorageBytes(out)!=text)throw std::runtime_error("noncanonical storage encoding");return out;
}
}
