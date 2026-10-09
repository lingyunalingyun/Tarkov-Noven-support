#include "plugins/PluginScanData.h"
#include "raid/RaidJson.h"
#include <charconv>
#include <limits>
#include <stdexcept>

namespace noven::plugins {
bool ValidScanId(std::string_view id){
    if(id.empty()||id.size()>20||id[0]=='0')return false;
    std::uint64_t value{};const auto parsed=std::from_chars(id.data(),id.data()+id.size(),value);
    return parsed.ec==std::errc{}&&parsed.ptr==id.data()+id.size()&&value!=0;
}
std::string ScanRecord(const data::RecentScanEntry& entry){
    // 持久化 uint64 身份以十进制字符串传输，避免 JSON/脚本数字丢失精度；不是时间戳身份。
    // Persisted uint64 identity travels as decimal text, avoiding JSON/script precision loss; not timestamp identity.
    using raid::json::Quote;
    auto text="{\"scanId\":"+Quote(std::to_string(entry.scanId))+",\"scannedAtUnixMs\":"+std::to_string(entry.scannedAtUnixMs)
        +",\"localSessionId\":"+(entry.localSessionId?Quote(*entry.localSessionId):"null")
        +",\"stableItemId\":"+Quote(entry.stableItemId)+",\"displayName\":"+Quote(entry.canonicalName)+"}";
    if(!ValidScanRecord(text))throw std::runtime_error("scan projection bounds");return text;
}
bool ValidScanRecord(std::string_view text) try {
    if(text.size()>8192)return false;
    const auto value=raid::json::Parser(text).Parse();
    if(value.object.size()!=5||!ValidScanId(value.At("scanId").String())||value.At("scannedAtUnixMs").Int()<=0)return false;
    const auto& item=value.At("stableItemId").String();const auto& name=value.At("displayName").String();
    if(item.empty()||item.size()>128||name.empty()||name.size()>4096)return false;
    const auto& session=value.At("localSessionId");
    return session.type==raid::json::Value::Type::Null||(session.type==raid::json::Value::Type::String&&!session.String().empty()&&session.String().size()<=256);
}catch(const std::exception&){return false;}
void ScanEventQueue::Drop(){auto count=dropped_.load();while(count!=(std::numeric_limits<std::uint32_t>::max)()&&!dropped_.compare_exchange_weak(count,count+1)){};}
void ScanEventQueue::Subscribe(bool enabled){std::lock_guard lock(mutex_);subscribed_=enabled;if(!enabled)pending_.clear();}
bool ScanEventQueue::Push(std::shared_ptr<const std::string> record){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock()){Drop();return false;}
    if(!subscribed_||!record)return false;
    if(pending_.size()==MaximumScanEvents){pending_.pop_front();Drop();}pending_.push_back(std::move(record));return true;
}
std::shared_ptr<const std::string> ScanEventQueue::Pop(){std::lock_guard lock(mutex_);if(pending_.empty())return {};auto record=std::move(pending_.front());pending_.pop_front();return record;}
bool ScanEventQueue::Subscribed() const {std::lock_guard lock(mutex_);return subscribed_;}
std::size_t ScanEventQueue::Pending() const {std::lock_guard lock(mutex_);return pending_.size();}
}
