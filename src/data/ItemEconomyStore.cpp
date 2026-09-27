#include "data/ItemEconomyStore.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace noven::data {

namespace {

struct JsonValue final {
    enum class Type { Null, Boolean, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::unordered_map<std::string, JsonValue>;

    Type type{Type::Null};
    bool boolean{};
    double number{};
    std::string string;
    Array array;
    Object object;

    [[nodiscard]] const JsonValue* Find(std::string_view key) const noexcept {
        if (type != Type::Object) {
            return nullptr;
        }
        const auto found = object.find(std::string(key));
        return found == object.end() ? nullptr : &found->second;
    }
};

void AppendUtf8(std::string& output, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

class JsonParser final {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool Parse(JsonValue& value, std::wstring& error) {
        SkipWhitespace();
        if (!ParseValue(value, error)) {
            return false;
        }
        SkipWhitespace();
        if (position_ != input_.size()) {
            error = L"JSON contains trailing data";
            return false;
        }
        return true;
    }

private:
    void SkipWhitespace() noexcept {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\t'
                && character != '\r' && character != '\n') {
                return;
            }
            ++position_;
        }
    }

    bool Consume(char expected) noexcept {
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    bool ParseValue(JsonValue& value, std::wstring& error) {
        SkipWhitespace();
        if (position_ >= input_.size()) {
            error = L"JSON ended before a value";
            return false;
        }
        switch (input_[position_]) {
        case 'n':
            return ParseLiteral("null", JsonValue::Type::Null, value, error);
        case 't':
            value.type = JsonValue::Type::Boolean;
            value.boolean = true;
            return ParseLiteral("true", JsonValue::Type::Boolean, value, error);
        case 'f':
            value.type = JsonValue::Type::Boolean;
            value.boolean = false;
            return ParseLiteral("false", JsonValue::Type::Boolean, value, error);
        case '"':
            value.type = JsonValue::Type::String;
            return ParseString(value.string, error);
        case '[':
            return ParseArray(value, error);
        case '{':
            return ParseObject(value, error);
        default:
            return ParseNumber(value, error);
        }
    }

    bool ParseLiteral(
        std::string_view literal,
        JsonValue::Type type,
        JsonValue& value,
        std::wstring& error
    ) {
        if (input_.substr(position_, literal.size()) != literal) {
            error = L"Invalid JSON literal";
            return false;
        }
        position_ += literal.size();
        value.type = type;
        return true;
    }

    bool ParseString(std::string& output, std::wstring& error) {
        if (!Consume('"')) {
            error = L"JSON string does not start with a quote";
            return false;
        }
        output.clear();
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return true;
            }
            if (character < 0x20) {
                error = L"JSON string contains a control character";
                return false;
            }
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                error = L"JSON escape is incomplete";
                return false;
            }
            const char escape = input_[position_++];
            switch (escape) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                std::uint32_t code_point = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    if (position_ >= input_.size()) {
                        error = L"JSON unicode escape is incomplete";
                        return false;
                    }
                    const char hexadecimal = input_[position_++];
                    code_point <<= 4;
                    if (hexadecimal >= '0' && hexadecimal <= '9') {
                        code_point += static_cast<unsigned>(hexadecimal - '0');
                    } else if (hexadecimal >= 'a' && hexadecimal <= 'f') {
                        code_point += static_cast<unsigned>(hexadecimal - 'a' + 10);
                    } else if (hexadecimal >= 'A' && hexadecimal <= 'F') {
                        code_point += static_cast<unsigned>(hexadecimal - 'A' + 10);
                    } else {
                        error = L"JSON unicode escape is invalid";
                        return false;
                    }
                }
                AppendUtf8(output, code_point);
                break;
            }
            default:
                error = L"JSON escape is invalid";
                return false;
            }
        }
        error = L"JSON string is unterminated";
        return false;
    }

    bool ParseArray(JsonValue& value, std::wstring& error) {
        Consume('[');
        value.type = JsonValue::Type::Array;
        value.array.clear();
        SkipWhitespace();
        if (Consume(']')) {
            return true;
        }
        while (true) {
            JsonValue child;
            if (!ParseValue(child, error)) {
                return false;
            }
            value.array.push_back(std::move(child));
            SkipWhitespace();
            if (Consume(']')) {
                return true;
            }
            if (!Consume(',')) {
                error = L"JSON array is missing a comma";
                return false;
            }
        }
    }

    bool ParseObject(JsonValue& value, std::wstring& error) {
        Consume('{');
        value.type = JsonValue::Type::Object;
        value.object.clear();
        SkipWhitespace();
        if (Consume('}')) {
            return true;
        }
        while (true) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key, error)) {
                return false;
            }
            SkipWhitespace();
            if (!Consume(':')) {
                error = L"JSON object is missing a colon";
                return false;
            }
            JsonValue child;
            if (!ParseValue(child, error)) {
                return false;
            }
            value.object.insert_or_assign(std::move(key), std::move(child));
            SkipWhitespace();
            if (Consume('}')) {
                return true;
            }
            if (!Consume(',')) {
                error = L"JSON object is missing a comma";
                return false;
            }
        }
    }

    bool ParseNumber(JsonValue& value, std::wstring& error) {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }
        while (position_ < input_.size()
               && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            while (position_ < input_.size()
                   && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < input_.size()
            && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size()
                && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            while (position_ < input_.size()
                   && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }
        if (start == position_) {
            error = L"JSON value is invalid";
            return false;
        }
        const std::string number(input_.substr(start, position_ - start));
        char* end = nullptr;
        value.number = std::strtod(number.c_str(), &end);
        if (end != number.c_str() + number.size() || !std::isfinite(value.number)) {
            error = L"JSON number is invalid";
            return false;
        }
        value.type = JsonValue::Type::Number;
        return true;
    }

    std::string_view input_;
    std::size_t position_{};
};

const JsonValue* Find(const JsonValue& value, std::string_view key) noexcept {
    return value.Find(key);
}

bool ReadString(const JsonValue* value, std::string& output) {
    if (value == nullptr || value->type != JsonValue::Type::String) {
        return false;
    }
    output = value->string;
    return true;
}

bool ReadNumber(const JsonValue* value, double& output) {
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return false;
    }
    output = value->number;
    return std::isfinite(output);
}

bool ReadInteger(const JsonValue* value, std::int64_t& output) {
    double number = 0.0;
    if (!ReadNumber(value, number)
        || number < static_cast<double>(std::numeric_limits<std::int64_t>::min())
        || number > static_cast<double>(std::numeric_limits<std::int64_t>::max())
        || std::floor(number) != number) {
        return false;
    }
    output = static_cast<std::int64_t>(number);
    return true;
}

bool ReadBoolean(const JsonValue* value, bool& output) {
    if (value == nullptr || value->type != JsonValue::Type::Boolean) {
        return false;
    }
    output = value->boolean;
    return true;
}

bool IsValidItemId(std::string_view id) {
    if (id.size() != 24) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f')
            || (character >= 'A' && character <= 'F');
    });
}

bool ContainsInsensitive(std::string value, std::string_view needle) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::string lower_needle(needle);
    std::transform(lower_needle.begin(), lower_needle.end(), lower_needle.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value.find(lower_needle) != std::string::npos;
}

std::chrono::system_clock::time_point ParseIsoTime(std::string_view value) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (value.size() < 19
        || std::sscanf(std::string(value).c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
                       &year, &month, &day, &hour, &minute, &second) != 6) {
        return {};
    }
    std::tm calendar{};
    calendar.tm_year = year - 1900;
    calendar.tm_mon = month - 1;
    calendar.tm_mday = day;
    calendar.tm_hour = hour;
    calendar.tm_min = minute;
    calendar.tm_sec = second;
    const std::time_t timestamp = _mkgmtime(&calendar);
    if (timestamp == static_cast<std::time_t>(-1)) {
        return {};
    }
    return std::chrono::system_clock::from_time_t(timestamp);
}

std::string IsoTime(std::chrono::system_clock::time_point value) {
    if (value == std::chrono::system_clock::time_point{}) {
        value = std::chrono::system_clock::now();
    }
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(value);
    std::tm calendar{};
    gmtime_s(&calendar, &timestamp);
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << calendar.tm_year + 1900 << '-'
           << std::setw(2) << calendar.tm_mon + 1 << '-'
           << std::setw(2) << calendar.tm_mday << 'T'
           << std::setw(2) << calendar.tm_hour << ':'
           << std::setw(2) << calendar.tm_min << ':'
           << std::setw(2) << calendar.tm_sec << 'Z';
    return output.str();
}

std::string EscapeJson(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 2);
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output.push_back(static_cast<char>(character)); break;
        }
    }
    output.push_back('"');
    return output;
}

bool ParseTraderValue(
    const JsonValue& value,
    const std::unordered_map<std::string, std::string>& trader_names,
    TraderSellValue& output
) {
    if (value.type != JsonValue::Type::Object) {
        return false;
    }
    const JsonValue* source = Find(value, "source");
    std::string source_name;
    if (ReadString(source, source_name) && ContainsInsensitive(source_name, "flea")) {
        return false;
    }

    const JsonValue* vendor = Find(value, "vendor");
    const JsonValue* trader = vendor != nullptr && vendor->type == JsonValue::Type::Object
        ? Find(*vendor, "trader") : nullptr;
    if (trader != nullptr && trader->type == JsonValue::Type::Object) {
        vendor = trader;
    }
    const auto read_vendor_field = [&](std::string_view key) -> const JsonValue* {
        return vendor != nullptr && vendor->type == JsonValue::Type::Object
            ? Find(*vendor, key) : nullptr;
    };
    ReadString(read_vendor_field("trader_id"), output.traderId);
    if (output.traderId.empty()) {
        ReadString(read_vendor_field("id"), output.traderId);
    }
    if (output.traderId.empty()) {
        ReadString(read_vendor_field("trader"), output.traderId);
    }
    ReadString(read_vendor_field("name"), output.traderName);
    if (output.traderName.empty()) {
        ReadString(Find(value, "traderName"), output.traderName);
    }
    if (output.traderId.empty()) {
        ReadString(Find(value, "trader"), output.traderId);
    }
    if (output.traderId.empty() || ContainsInsensitive(output.traderId, "flea")) {
        return false;
    }
    if (output.traderName.empty()) {
        const auto name = trader_names.find(output.traderId);
        if (name != trader_names.end()) output.traderName = name->second;
    }
    if (output.traderName.empty()) return false;

    std::int64_t price = 0;
    if (!ReadInteger(Find(value, "priceRUB"), price)) {
        std::string currency;
        if (ReadString(Find(value, "currency"), currency)
            && !ContainsInsensitive(currency, "rub")) {
            return false;
        }
        if (!ReadInteger(Find(value, "price"), price)) {
            return false;
        }
    }
    if (price <= 0 || price > 1'000'000'000'000LL) {
        return false;
    }
    output.priceRoubles = price;
    return true;
}

FleaStatus ParseFleaStatus(std::string_view status) {
    if (status == "Allowed") return FleaStatus::Allowed;
    if (status == "Banned") return FleaStatus::Banned;
    if (status == "LockedOrUnavailable") return FleaStatus::LockedOrUnavailable;
    return FleaStatus::Unknown;
}

const char* FleaStatusValue(FleaStatus status) noexcept {
    switch (status) {
    case FleaStatus::Allowed: return "Allowed";
    case FleaStatus::Banned: return "Banned";
    case FleaStatus::LockedOrUnavailable: return "LockedOrUnavailable";
    case FleaStatus::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace

const wchar_t* FleaStatusName(FleaStatus status) noexcept {
    switch (status) {
    case FleaStatus::Allowed: return L"Allowed";
    case FleaStatus::Banned: return L"Banned";
    case FleaStatus::LockedOrUnavailable: return L"LockedOrUnavailable";
    case FleaStatus::Unknown: return L"Unknown";
    }
    return L"Unknown";
}

ItemEconomyStore::ModeCache& ItemEconomyStore::Cache(GameMode mode) noexcept {
    switch (mode) {
    case GameMode::Pvp: return pvp_;
    case GameMode::Pve: return pve_;
    case GameMode::Seasonal: return seasonal_;
    }
    return pvp_;
}

const ItemEconomyStore::ModeCache& ItemEconomyStore::Cache(GameMode mode) const noexcept {
    switch (mode) {
    case GameMode::Pvp: return pvp_;
    case GameMode::Pve: return pve_;
    case GameMode::Seasonal: return seasonal_;
    }
    return pvp_;
}

bool ItemEconomyStore::ReplaceFromUpstreamJson(
    GameMode mode,
    std::string_view payload,
    std::wstring& error,
    std::string_view trader_names_payload
) {
    // 在临时模式缓存中解析并校验整份数据，成功后才替换现有价格快照。
    // Parse and validate the full payload in a temporary mode cache;
    // replace the existing price snapshot only on success.
    JsonValue root;
    JsonParser parser(payload);
    if (!parser.Parse(root, error)) {
        return false;
    }
    const JsonValue* data = Find(root, "data");
    const JsonValue* items = Find(data != nullptr ? *data : root, "items");
    if (items == nullptr || items->type != JsonValue::Type::Object
        || items->object.empty() || items->object.size() > 100'000) {
        error = L"Economy JSON has no plausible items object";
        return false;
    }

    std::unordered_map<std::string, std::string> trader_names;
    if (!trader_names_payload.empty()) {
        JsonValue names_root;
        JsonParser names_parser(trader_names_payload);
        if (!names_parser.Parse(names_root, error)) return false;
        const JsonValue* names = Find(names_root, "data");
        if (names == nullptr || names->type != JsonValue::Type::Object) {
            error = L"Trader translation JSON has no data object";
            return false;
        }
        constexpr std::string_view suffix = " Nickname";
        for (const auto& [key, value] : names->object) {
            if (!key.ends_with(suffix)) continue;
            const std::string id = key.substr(0, key.size() - suffix.size());
            std::string name;
            if (IsValidItemId(id) && ReadString(&value, name) && !name.empty()) {
                trader_names.emplace(id, std::move(name));
            }
        }
        if (trader_names.empty()) {
            error = L"Trader translation JSON has no usable nicknames";
            return false;
        }
    }

    const JsonValue* flea_market = Find(data != nullptr ? *data : root, "fleaMarket");
    bool flea_enabled = false;
    const bool has_flea_status = flea_market != nullptr
        && ReadBoolean(Find(*flea_market, "enabled"), flea_enabled);

    ModeCache candidate;
    candidate.source = "json.tarkov.dev";
    candidate.upstream_mode = UpstreamGameModeCode(mode);
    candidate.schema_version = 1;
    candidate.fetched_at = std::chrono::system_clock::now();
    candidate.items.reserve(items->object.size());

    for (const auto& [key, item] : items->object) {
        const JsonValue* id_value = Find(item, "id");
        std::string id = key;
        if (id_value != nullptr) {
            ReadString(id_value, id);
        }
        if (!IsValidItemId(id)) {
            continue;
        }

        ItemEconomyInfo info;
        info.itemId = id;
        std::int64_t integer = 0;
        if (ReadInteger(Find(item, "width"), integer) && integer > 0 && integer <= 100) {
            info.width = static_cast<int>(integer);
        }
        if (ReadInteger(Find(item, "height"), integer) && integer > 0 && integer <= 100) {
            info.height = static_cast<int>(integer);
        }
        double changeAmount = 0;
        if (ReadNumber(Find(item, "changeLast48h"), changeAmount)) info.fleaChangeAmount = changeAmount;
        if (ReadInteger(Find(item, "lastLowPrice"), integer)
            && integer > 0 && integer <= 1'000'000'000'000LL) {
            info.fleaPrice = integer;
        }

        bool no_flea = false;
        const JsonValue* types = Find(item, "types");
        if (types != nullptr && types->type == JsonValue::Type::Array) {
            for (const JsonValue& type : types->array) {
                std::string type_name;
                if (ReadString(&type, type_name) && type_name == "noFlea") {
                    no_flea = true;
                }
            }
        }
        if (no_flea) {
            info.fleaStatus = FleaStatus::Banned;
        } else if (has_flea_status && flea_enabled) {
            info.fleaStatus = FleaStatus::Allowed;
        }

        const auto parse_offer_array = [&](const JsonValue* offers) {
            if (offers == nullptr || offers->type != JsonValue::Type::Array) {
                return;
            }
            for (const JsonValue& offer : offers->array) {
                TraderSellValue trader_value;
                if (ParseTraderValue(offer, trader_names, trader_value)
                    && (!info.bestTrader.has_value()
                        || trader_value.priceRoubles > info.bestTrader->priceRoubles)) {
                    info.bestTrader = std::move(trader_value);
                }
            }
        };
        // 现行静态 JSON 使用 sellToTrader；priceRUB 是原币报价的卢布换算值。
        // Current static JSON uses sellToTrader; priceRUB is the RUB equivalent.
        parse_offer_array(Find(item, "sellToTrader"));
        parse_offer_array(Find(item, "sellFor"));
        parse_offer_array(Find(item, "traderPrices"));

        if (info.fleaPrice.has_value() || info.bestTrader.has_value()) {
            info.bestValue = std::max(
                info.fleaPrice.value_or(0),
                info.bestTrader.has_value() ? info.bestTrader->priceRoubles : 0
            );
            if (info.width > 0 && info.height > 0 && info.bestValue.value() > 0) {
                info.valuePerSlot = static_cast<double>(*info.bestValue)
                    / static_cast<double>(info.width * info.height);
            }
        }

        std::string updated;
        if (ReadString(Find(item, "updated"), updated)) {
            info.updatedAt = ParseIsoTime(updated);
        }
        if (info.updatedAt == std::chrono::system_clock::time_point{}) {
            info.updatedAt = candidate.fetched_at;
        }
        candidate.items.insert_or_assign(info.itemId, std::move(info));
    }

    if (candidate.items.empty()) {
        error = L"Economy JSON produced no valid items";
        return false;
    }
    Cache(mode) = std::move(candidate);
    return true;
}

bool ItemEconomyStore::LoadCacheFile(
    GameMode mode,
    const std::filesystem::path& path,
    std::wstring& error
) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = L"Economy cache file is unavailable";
        return false;
    }
    const std::string payload(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    JsonValue root;
    JsonParser parser(payload);
    if (!parser.Parse(root, error) || root.type != JsonValue::Type::Object) {
        if (error.empty()) error = L"Economy cache JSON is invalid";
        return false;
    }
    std::int64_t schema = 0;
    if (!ReadInteger(Find(root, "schemaVersion"), schema) || schema != 1) {
        error = L"Economy cache schema version is unsupported";
        return false;
    }
    std::string upstream_mode;
    std::string source;
    std::int64_t declared_item_count = 0;
    const JsonValue* item_array = Find(root, "items");
    if (!ReadString(Find(root, "gameMode"), upstream_mode)
        || upstream_mode != UpstreamGameModeCode(mode)
        || !ReadString(Find(root, "source"), source)
        || !ReadInteger(Find(root, "itemCount"), declared_item_count)
        || source.empty()
        || item_array == nullptr || item_array->type != JsonValue::Type::Array
        || item_array->array.empty() || item_array->array.size() > 100'000
        || declared_item_count != static_cast<std::int64_t>(item_array->array.size())) {
        error = L"Economy cache metadata or item list is invalid";
        return false;
    }

    ModeCache candidate;
    candidate.schema_version = 1;
    candidate.source = std::move(source);
    candidate.upstream_mode = std::move(upstream_mode);
    std::string fetched_at;
    candidate.fetched_at = ReadString(Find(root, "fetchedAt"), fetched_at)
        ? ParseIsoTime(fetched_at) : std::chrono::system_clock::time_point{};
    if (candidate.fetched_at == std::chrono::system_clock::time_point{}) {
        error = L"Economy cache timestamp is invalid";
        return false;
    }
    candidate.items.reserve(item_array->array.size());
    for (const JsonValue& item : item_array->array) {
        std::string id;
        if (!ReadString(Find(item, "id"), id) || !IsValidItemId(id)) {
            error = L"Economy cache contains an invalid item ID";
            return false;
        }
        ItemEconomyInfo info;
        info.itemId = id;
        std::int64_t integer = 0;
        if (ReadInteger(Find(item, "width"), integer) && integer > 0 && integer <= 100) {
            info.width = static_cast<int>(integer);
        }
        if (ReadInteger(Find(item, "height"), integer) && integer > 0 && integer <= 100) {
            info.height = static_cast<int>(integer);
        }
        double changeAmount = 0;
        if (ReadNumber(Find(item, "fleaChangeAmount"), changeAmount)) info.fleaChangeAmount = changeAmount;
        if (ReadInteger(Find(item, "fleaPrice"), integer) && integer > 0) {
            info.fleaPrice = integer;
        }
        std::string status;
        if (ReadString(Find(item, "fleaStatus"), status)) {
            info.fleaStatus = ParseFleaStatus(status);
        }
        const JsonValue* trader = Find(item, "bestTrader");
        if (trader != nullptr && trader->type == JsonValue::Type::Object) {
            TraderSellValue value;
            if (ReadString(Find(*trader, "traderId"), value.traderId)
                && !value.traderId.empty()
                && ReadInteger(Find(*trader, "priceRoubles"), value.priceRoubles)
                && value.priceRoubles > 0) {
                ReadString(Find(*trader, "traderName"), value.traderName);
                info.bestTrader = std::move(value);
            }
        }
        if (ReadInteger(Find(item, "bestValue"), integer) && integer > 0) {
            info.bestValue = integer;
        } else if (info.fleaPrice.has_value() || info.bestTrader.has_value()) {
            info.bestValue = std::max(
                info.fleaPrice.value_or(0),
                info.bestTrader.has_value() ? info.bestTrader->priceRoubles : 0
            );
        }
        double value_per_slot = 0.0;
        if (ReadNumber(Find(item, "valuePerSlot"), value_per_slot)
            && value_per_slot > 0.0 && std::isfinite(value_per_slot)) {
            info.valuePerSlot = value_per_slot;
        } else if (info.bestValue.has_value() && info.width > 0 && info.height > 0) {
            info.valuePerSlot = static_cast<double>(*info.bestValue)
                / static_cast<double>(info.width * info.height);
        }
        std::string updated_at;
        info.updatedAt = ReadString(Find(item, "updatedAt"), updated_at)
            ? ParseIsoTime(updated_at) : candidate.fetched_at;
        if (info.updatedAt == std::chrono::system_clock::time_point{}) {
            info.updatedAt = candidate.fetched_at;
        }
        candidate.items.insert_or_assign(std::move(id), std::move(info));
    }
    if (candidate.items.empty()) {
        error = L"Economy cache contains no valid items";
        return false;
    }
    Cache(mode) = std::move(candidate);
    return true;
}

bool ItemEconomyStore::SaveCacheFile(
    GameMode mode,
    const std::filesystem::path& path,
    std::wstring& error
) const {
    // 先写临时文件再替换磁盘缓存，避免部分写入损坏离线可用数据。
    // Write a temporary file before replacing disk cache so partial output
    // cannot corrupt the last usable offline snapshot.
    const ModeCache& cache = Cache(mode);
    if (cache.items.empty() || cache.upstream_mode.empty()) {
        error = L"There is no valid economy cache to save";
        return false;
    }
    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error) {
        error = L"Could not create economy cache directory";
        return false;
    }
    const std::filesystem::path temporary_path = path.wstring() + L".tmp";
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = L"Could not open temporary economy cache";
        return false;
    }
    output << "{\"schemaVersion\":1,\"source\":" << EscapeJson(cache.source)
           << ",\"gameMode\":" << EscapeJson(cache.upstream_mode)
           << ",\"fetchedAt\":" << EscapeJson(IsoTime(cache.fetched_at))
           << ",\"itemCount\":" << cache.items.size() << ",\"items\":[";
    bool first = true;
    output << std::setprecision(17);
    for (const auto& [id, info] : cache.items) {
        if (!first) output << ',';
        first = false;
        output << "{\"id\":" << EscapeJson(id)
               << ",\"width\":" << info.width
               << ",\"height\":" << info.height
               << ",\"fleaStatus\":" << EscapeJson(FleaStatusValue(info.fleaStatus))
               << ",\"updatedAt\":" << EscapeJson(IsoTime(info.updatedAt));
        if (info.fleaPrice.has_value()) output << ",\"fleaPrice\":" << *info.fleaPrice;
        if (info.fleaChangeAmount) output << ",\"fleaChangeAmount\":" << *info.fleaChangeAmount;
        if (info.bestTrader.has_value()) {
            output << ",\"bestTrader\":{\"traderId\":"
                   << EscapeJson(info.bestTrader->traderId)
                   << ",\"traderName\":" << EscapeJson(info.bestTrader->traderName)
                   << ",\"priceRoubles\":" << info.bestTrader->priceRoubles << '}';
        }
        if (info.bestValue.has_value()) output << ",\"bestValue\":" << *info.bestValue;
        if (info.valuePerSlot.has_value()) output << ",\"valuePerSlot\":" << *info.valuePerSlot;
        output << '}';
    }
    output << "]}";
    output.flush();
    if (!output) {
        output.close();
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        error = L"Could not write temporary economy cache";
        return false;
    }
    output.close();
    if (!MoveFileExW(
        temporary_path.c_str(),
        path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    )) {
        std::error_code remove_error;
        std::filesystem::remove(temporary_path, remove_error);
        error = L"Could not atomically replace economy cache";
        return false;
    }
    return true;
}

const ItemEconomyInfo* ItemEconomyStore::Lookup(
    GameMode mode,
    std::string_view item_id
) const noexcept {
    const auto& items = Cache(mode).items;
    const auto found = items.find(std::string(item_id));
    return found == items.end() ? nullptr : &found->second;
}

std::chrono::system_clock::time_point ItemEconomyStore::GetLastUpdated(
    GameMode mode
) const noexcept {
    return Cache(mode).fetched_at;
}

std::size_t ItemEconomyStore::ItemCount(GameMode mode) const noexcept {
    return Cache(mode).items.size();
}

std::size_t ItemEconomyStore::TraderItemCount(GameMode mode) const noexcept {
    std::size_t count = 0;
    for (const auto& [id, item] : Cache(mode).items) {
        if (item.bestTrader.has_value()) ++count;
    }
    return count;
}

} // namespace noven::data
