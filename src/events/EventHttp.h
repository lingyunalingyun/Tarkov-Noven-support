#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace noven::events {
inline constexpr std::size_t kMaximumResponseBytes=2*1024*1024;
struct HttpResponse { unsigned status{};std::string contentType,body,etag,lastModified,error; };
class IEventHttp {
public:
    virtual ~IEventHttp()=default;
    virtual HttpResponse Get(std::wstring_view host,std::wstring_view path,
        std::string_view etag={},std::string_view lastModified={})=0;
};
class WinHttpEventClient final : public IEventHttp {
public:
    HttpResponse Get(std::wstring_view,std::wstring_view,std::string_view={},std::string_view={}) override;
};
bool ValidateHtml(const HttpResponse&,std::string& error);
}
