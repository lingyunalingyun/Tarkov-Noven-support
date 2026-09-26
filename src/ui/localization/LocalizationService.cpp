#include "ui/localization/LocalizationService.h"
#include "ui/localization/TextKeys.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

namespace noven::ui {
namespace {
std::wstring Wide(std::string_view input) {
    if (input.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (!count) throw std::runtime_error("invalid UTF-8");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), result.data(), count);
    return result;
}
struct Value {
    bool isString{};
    std::wstring text;
    std::map<std::wstring, Value> fields;
};

// 语言包仅允许对象和字符串，按键存储而非按字段顺序解析；拒绝重复键。
// Packs allow objects and strings only, parsed by key rather than field order; duplicate keys are rejected.
class Reader {
public:
    explicit Reader(std::wstring text) : text_(std::move(text)) {}
    Value Read(unsigned depth = 0) {
        Space();
        if (depth > 2) Fail();
        if (Peek() == L'"') return {true, String(), {}};
        Expect(L'{');
        Value result;
        if (Take(L'}')) return result;
        do {
            auto key = String();
            Expect(L':');
            if (!result.fields.emplace(std::move(key), Read(depth + 1)).second) Fail();
            if (Take(L'}')) return result;
            Expect(L',');
        } while (true);
    }
    bool End() { Space(); return pos_ == text_.size(); }
private:
    [[noreturn]] static void Fail() { throw std::runtime_error("invalid locale JSON"); }
    void Space() { while (pos_ < text_.size() && (text_[pos_] == L' ' || text_[pos_] == L'\r'
        || text_[pos_] == L'\n' || text_[pos_] == L'\t')) ++pos_; }
    wchar_t Peek() { Space(); return pos_ < text_.size() ? text_[pos_] : 0; }
    bool Take(wchar_t c) { if (Peek() != c) return false; ++pos_; return true; }
    void Expect(wchar_t c) { if (!Take(c)) Fail(); }
    wchar_t Hex() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ == text_.size()) Fail();
            const wchar_t c = text_[pos_++];
            const int digit = c >= L'0' && c <= L'9' ? c - L'0'
                : c >= L'a' && c <= L'f' ? c - L'a' + 10
                : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
            if (digit < 0) Fail();
            value = value * 16 + digit;
        }
        return static_cast<wchar_t>(value);
    }
    std::wstring String() {
        Expect(L'"');
        std::wstring value;
        while (pos_ < text_.size()) {
            wchar_t c = text_[pos_++];
            if (c == L'"') return value;
            if (c < 0x20) Fail();
            if (c == L'\\') {
                if (pos_ == text_.size()) Fail();
                c = text_[pos_++];
                switch (c) {
                case L'"': case L'\\': case L'/': break;
                case L'b': c = L'\b'; break;
                case L'f': c = L'\f'; break;
                case L'n': c = L'\n'; break;
                case L'r': c = L'\r'; break;
                case L't': c = L'\t'; break;
                case L'u': {
                    c = Hex();
                    if (c >= 0xD800 && c <= 0xDBFF) {
                        if (pos_ + 2 > text_.size() || text_[pos_++] != L'\\' || text_[pos_++] != L'u') Fail();
                        const auto low = Hex();
                        if (low < 0xDC00 || low > 0xDFFF) Fail();
                        value.push_back(c); c = low;
                    } else if (c >= 0xDC00 && c <= 0xDFFF) Fail();
                    break;
                }
                default: Fail();
                }
            }
            value.push_back(c);
        }
        Fail();
    }
    std::wstring text_;
    std::size_t pos_{};
};

std::set<std::wstring> Placeholders(const std::wstring& text) {
    static const std::wregex name(L"[A-Za-z_][A-Za-z0-9_]*");
    std::set<std::wstring> result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'}') throw std::runtime_error("unmatched brace");
        if (text[i] != L'{') continue;
        const auto end = text.find(L'}', i);
        if (end == std::wstring::npos) throw std::runtime_error("unmatched brace");
        auto token = text.substr(i + 1, end - i - 1);
        if (!std::regex_match(token, name)) throw std::runtime_error("invalid placeholder");
        result.insert(std::move(token)); i = end;
    }
    return result;
}
struct Pack { LocaleInfo info; std::map<std::wstring, std::wstring> strings; };
Pack Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("unreadable pack");
    const std::string bytes{std::istreambuf_iterator<char>(file), {}};
    Reader reader(Wide(bytes));
    auto root = reader.Read();
    if (!reader.End() || root.isString) throw std::runtime_error("invalid root");
    const auto& meta = root.fields.at(L"_meta");
    if (meta.isString) throw std::runtime_error("invalid metadata");
    const auto field = [&](const wchar_t* key) {
        const auto& v = meta.fields.at(key);
        if (!v.isString || v.text.empty()) throw std::runtime_error("invalid metadata");
        return v.text;
    };
    const auto locale = field(L"locale");
    if (!std::regex_match(locale, std::wregex(L"[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*"))
        || path.stem().wstring() != locale) throw std::runtime_error("invalid locale identifier/filename");
    std::string identifier;
    for (const auto c : locale) identifier.push_back(static_cast<char>(c));
    Pack pack{{identifier, field(L"name"), field(L"nameEnglish")}, {}};
    for (const auto& [key, value] : root.fields) {
        if (key == L"_meta") continue;
        if (key.empty() || !value.isString || value.text.empty()) throw std::runtime_error("invalid text value");
        pack.strings.emplace(key, value.text);
    }
    return pack;
}
}

bool LocalizationService::DiscoverLocales(const std::filesystem::path& directory, std::wstring& error) {
    dictionaries_.clear(); available_.clear(); warnings_.clear(); active_ = "zh-CN";
    error.clear();
    try {
        auto source = Load(directory / L"zh-CN.json");
        for (const auto key : TextKey::All)
            if (!source.strings.contains(Wide(key))) throw std::runtime_error("source key missing");
        for (const auto& [key, value] : source.strings) { (void)key; Placeholders(value); }
        dictionaries_.emplace(source.info.locale, source.strings);
        available_.push_back(source.info);
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.is_regular_file() && entry.path().extension() == L".json"
                && entry.path().filename() != L"zh-CN.json") paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) {
            try {
                auto pack = Load(path);
                if (dictionaries_.contains(pack.info.locale)) throw std::runtime_error("duplicate locale");
                for (auto it = pack.strings.begin(); it != pack.strings.end();) {
                    const auto original = source.strings.find(it->first);
                    bool valid = original != source.strings.end();
                    try { valid = valid && Placeholders(it->second) == Placeholders(original->second); }
                    catch (const std::exception&) { valid = false; }
                    if (!valid) { warnings_.push_back(path.filename().wstring() + L": ignored key " + it->first); it = pack.strings.erase(it); }
                    else ++it;
                }
                dictionaries_.emplace(pack.info.locale, std::move(pack.strings));
                available_.push_back(std::move(pack.info));
            } catch (const std::exception& e) {
                warnings_.push_back(path.filename().wstring() + L": " + Wide(e.what()));
            }
        }
        for (const auto& warning : warnings_) OutputDebugStringW((L"[i18n] " + warning + L"\n").c_str());
        return true;
    } catch (const std::exception& e) {
        dictionaries_.clear(); available_.clear();
        error = L"[i18n] source initialization failed: " + Wide(e.what());
        return false;
    }
}

bool LocalizationService::SetLocale(std::string_view locale) {
    if (!dictionaries_.contains(std::string(locale))) return false;
    active_ = locale;
    return true;
}
std::wstring LocalizationService::Get(std::string_view key) const {
    const auto wide = Wide(key);
    // 不回退到英语；缺失源文字时显示诊断，而不是空白。
    // Never fall back to English; missing source text produces a visible diagnostic, not a blank.
    for (const auto& locale : {active_, std::string("zh-CN")}) {
        const auto pack = dictionaries_.find(locale);
        if (pack != dictionaries_.end()) {
            const auto found = pack->second.find(wide);
            if (found != pack->second.end()) return found->second;
        }
    }
    return L"[missing:" + wide + L"]";
}
std::wstring LocalizationService::Format(std::string_view key, std::initializer_list<Argument> arguments) const {
    const auto text = Get(key);
    std::wstring output;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != L'{') { output += text[i++]; continue; }
        const auto end = text.find(L'}', i);
        if (end == std::wstring::npos) { output.append(text, i); break; }
        const auto name = std::wstring_view(text).substr(i + 1, end - i - 1);
        const auto found = std::find_if(arguments.begin(), arguments.end(), [&](const auto& arg) { return arg.first == name; });
        if (found == arguments.end()) output.append(text, i, end - i + 1);
        else output += found->second;
        i = end + 1;
    }
    return output;
}
LocalizationService& UiLocalization() { static LocalizationService service; return service; }
} // namespace noven::ui
