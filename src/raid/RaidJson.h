#pragma once
#include <windows.h>
#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace noven::raid::json {
// 本 schema 只使用整数；对象与键顺序无关，重复键、非法 UTF-8 和过深输入均拒绝。
// This schema uses integers only; object order is irrelevant, duplicate keys/invalid UTF-8/deep input reject.
struct Value {
    enum class Type { Null, Boolean, Integer, String, Array, Object, Number } type{Type::Null};
    bool boolean{}; std::int64_t integer{}; std::string text;
    std::vector<Value> array; std::map<std::string, Value> object;
    const Value& At(const char* key) const {
        if (type != Type::Object) throw std::runtime_error("expected object");
        const auto it = object.find(key);
        if (it == object.end()) throw std::runtime_error("missing schema key");
        return it->second;
    }
    std::int64_t Int() const { if (type != Type::Integer) throw std::runtime_error("expected integer"); return integer; }
    bool Bool() const { if (type != Type::Boolean) throw std::runtime_error("expected bool"); return boolean; }
    const std::string& String() const { if (type != Type::String) throw std::runtime_error("expected string"); return text; }
    const std::vector<Value>& Array() const { if (type != Type::Array) throw std::runtime_error("expected array"); return array; }
};
class Parser {
public:
    // 网络接口可包含非 schema 数字；默认仍严格拒绝小数，存储契约不变。
    // Remote payloads may contain non-schema numbers; decimals remain rejected by default for storage.
    explicit Parser(std::string_view input,bool allowNumbers=false) : input_(input),allowNumbers_(allowNumbers) {}
    Value Parse() {
        if (input_.size() > 64*1024*1024 || (!input_.empty() && !MultiByteToWideChar(CP_UTF8,
            MB_ERR_INVALID_CHARS,input_.data(),static_cast<int>(input_.size()),nullptr,0))) Fail();
        auto value = Read(0); Space(); if (pos_ != input_.size()) Fail(); return value;
    }
private:
    [[noreturn]] static void Fail() { throw std::runtime_error("invalid raid JSON"); }
    void Space() { while (pos_ < input_.size() && std::string_view(" \r\n\t").find(input_[pos_]) != std::string_view::npos) ++pos_; }
    bool Take(char c) { Space(); if (pos_ < input_.size() && input_[pos_] == c) { ++pos_; return true; } return false; }
    unsigned Hex() {
        if (pos_ + 4 > input_.size()) Fail(); unsigned code{};
        for (int i=0;i<4;++i) { const char c=input_[pos_++]; code <<= 4;
            if(c>='0'&&c<='9')code+=c-'0'; else if(c>='a'&&c<='f')code+=c-'a'+10;
            else if(c>='A'&&c<='F')code+=c-'A'+10; else Fail(); }
        return code;
    }
    std::string String() {
        if (!Take('"')) Fail(); std::string text;
        while (pos_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return text;
            if (c < 32) Fail();
            if (c != '\\') { text += static_cast<char>(c); continue; }
            if (pos_ == input_.size()) Fail();
            switch (input_[pos_++]) {
            case '"':text+='"';break; case '\\':text+='\\';break; case '/':text+='/';break;
            case 'b':text+='\b';break; case 'f':text+='\f';break; case 'n':text+='\n';break;
            case 'r':text+='\r';break; case 't':text+='\t';break;
            case 'u': {
                unsigned code = Hex(); wchar_t units[2]{static_cast<wchar_t>(code),0}; int count=1;
                if(code>=0xD800&&code<=0xDBFF) {
                    if(pos_+2>input_.size()||input_.substr(pos_,2)!="\\u")Fail(); pos_+=2;
                    code=Hex(); if(code<0xDC00||code>0xDFFF)Fail(); units[1]=static_cast<wchar_t>(code);count=2;
                } else if(code>=0xDC00&&code<=0xDFFF)Fail();
                char bytes[8]{}; const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,units,count,bytes,8,nullptr,nullptr);
                if(!size)Fail(); text.append(bytes,static_cast<std::size_t>(size));break;
            }
            default:Fail(); }
        }
        Fail();
    }
    Value Read(int depth) {
        if(depth>32||++nodes_>1000000)Fail(); Space(); if(pos_==input_.size())Fail();
        Value v;
        if(Take('{')) { v.type=Value::Type::Object; if(Take('}'))return v;
            do { auto key=String();if(!Take(':'))Fail();auto child=Read(depth+1);
                if(!v.object.emplace(std::move(key),std::move(child)).second)Fail(); } while(Take(','));
            if(!Take('}'))Fail();return v; }
        if(Take('[')) { v.type=Value::Type::Array; if(Take(']'))return v;
            do { v.array.push_back(Read(depth+1)); } while(Take(',')); if(!Take(']'))Fail();return v; }
        if(input_[pos_]=='"') { v.type=Value::Type::String;v.text=String();return v; }
        for(const auto token : {std::string_view("null"),std::string_view("true"),std::string_view("false")})
            if(input_.substr(pos_,token.size())==token) {pos_+=token.size();if(token!="null") {v.type=Value::Type::Boolean;v.boolean=token=="true";}return v;}
        const auto start=pos_;if(input_[pos_]=='-')++pos_;
        const auto digits=pos_;while(pos_<input_.size()&&input_[pos_]>='0'&&input_[pos_]<='9')++pos_;
        if(pos_==digits||(pos_-digits>1&&input_[digits]=='0'))Fail();
        if(allowNumbers_ && pos_<input_.size() && (input_[pos_]=='.'||input_[pos_]=='e'||input_[pos_]=='E')) {
            if(input_[pos_]=='.') {
                const auto fraction=++pos_;while(pos_<input_.size()&&input_[pos_]>='0'&&input_[pos_]<='9')++pos_;
                if(pos_==fraction)Fail();
            }
            if(pos_<input_.size()&&(input_[pos_]=='e'||input_[pos_]=='E')) {
                ++pos_;if(pos_<input_.size()&&(input_[pos_]=='+'||input_[pos_]=='-'))++pos_;
                const auto exponent=pos_;while(pos_<input_.size()&&input_[pos_]>='0'&&input_[pos_]<='9')++pos_;
                if(pos_==exponent)Fail();
            }
            v.type=Value::Type::Number;v.text=input_.substr(start,pos_-start);return v;
        }
        const auto [end,ec]=std::from_chars(input_.data()+start,input_.data()+pos_,v.integer);
        if(ec!=std::errc{}||end!=input_.data()+pos_)Fail();v.type=Value::Type::Integer;return v;
    }
    std::string_view input_;std::size_t pos_{},nodes_{};bool allowNumbers_{};
};
inline std::string Quote(std::string_view text) {
    std::string value("\"");constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:text) { if(c=='"'||c=='\\') {value+='\\';value+=static_cast<char>(c);}
        else if(c<32) {value+="\\u00";value+=hex[c>>4];value+=hex[c&15];} else value+=static_cast<char>(c); }
    return value+'"';
}
}
