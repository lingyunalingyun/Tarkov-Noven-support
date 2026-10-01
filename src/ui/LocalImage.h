#pragma once
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <array>
#include <algorithm>
#include <optional>

namespace noven::ui {
// 概览使用缩略底图，放大时按需解码可见高清块；缓存有界且属于创建它的 target。
// Overview uses a preview; zoom decodes visible full-resolution tiles into a bounded target-owned cache.
class LocalImage final {
public:
    bool Load(const std::filesystem::path& file){
        // 仅在解码期间平衡 COM；已有其他 apartment 时复用线程已有初始化。
        // Balance COM during decoding; reuse an existing apartment if its model differs.
        struct Apartment {
            HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
        } apartment;
        if(FAILED(apartment.result)&&apartment.result!=RPC_E_CHANGED_MODE)return false;
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;ComPtr<IWICBitmapScaler> scaler;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
            ||FAILED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))
            ||FAILED(decoder->GetFrame(0,&frame)))return false;
        UINT width=0,height=0;
        if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>8192||height>8192
            ||FAILED(factory->CreateFormatConverter(&converter))
            ||FAILED(factory->CreateBitmapScaler(&scaler)))return false;
        const float ratio=std::min(1.0F,2048.0F/static_cast<float>(std::max(width,height)));
        const UINT previewWidth=std::max(1U,static_cast<UINT>(width*ratio)),previewHeight=std::max(1U,static_cast<UINT>(height*ratio));
        if(FAILED(scaler->Initialize(frame.Get(),previewWidth,previewHeight,WICBitmapInterpolationModeFant))
            ||FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
        std::vector<BYTE> pixels(static_cast<std::size_t>(previewWidth)*previewHeight*4);
        if(FAILED(converter->CopyPixels(nullptr,previewWidth*4,static_cast<UINT>(pixels.size()),pixels.data())))return false;
        pixels_=std::move(pixels);width_=previewWidth;height_=previewHeight;
        sourceWidth_=width;sourceHeight_=height;file_=file;devices_={};tiles_={};return true;
    }
    bool Ready() const noexcept{return !pixels_.empty();}
    bool Draw(ID2D1RenderTarget& target,D2D1_RECT_F rectangle,float opacity=1,
        std::optional<D2D1_RECT_F> clip=std::nullopt) const {
        if(!Ready())return false;
        float dpiX=96,dpiY=96;target.GetDpi(&dpiX,&dpiY);
        if(sourceWidth_>width_&&((rectangle.right-rectangle.left)*dpiX/96>width_*1.5F
            ||(rectangle.bottom-rectangle.top)*dpiY/96>height_*1.5F))
            return DrawTiles(target,rectangle,opacity,clip.value_or(rectangle));
        // 主窗口与页面转场可使用不同 target；每个 bitmap 保留其创建 target 的身份。
        // Shell and page transitions may use different targets; each bitmap retains its creating target.
        for(auto& entry:devices_)if(entry.target.Get()==&target){
            target.DrawBitmap(entry.bitmap.Get(),rectangle,opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);return true;}
        auto& entry=devices_[next_];next_=(next_+1)%devices_.size();
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        if(FAILED(target.CreateBitmap(D2D1::SizeU(width_,height_),pixels_.data(),width_*4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&bitmap)))return false;
        entry.target=&target;entry.bitmap=std::move(bitmap);
        target.DrawBitmap(entry.bitmap.Get(),rectangle,opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);return true;
    }
private:
    bool DrawTiles(ID2D1RenderTarget& target,D2D1_RECT_F rectangle,float opacity,D2D1_RECT_F clip) const {
        struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}} apartment;
        if(FAILED(apartment.result)&&apartment.result!=RPC_E_CHANGED_MODE)return false;
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
        const float sx=(rectangle.right-rectangle.left)/sourceWidth_,sy=(rectangle.bottom-rectangle.top)/sourceHeight_;
        if(sx<=0||sy<=0)return false;
        constexpr UINT tileSize=512;
        const auto stamp=++stamp_;
        for(UINT y=0;y<sourceHeight_;y+=tileSize)for(UINT x=0;x<sourceWidth_;x+=tileSize){
            const UINT w=std::min(tileSize,sourceWidth_-x),h=std::min(tileSize,sourceHeight_-y);
            const D2D1_RECT_F destination{rectangle.left+x*sx,rectangle.top+y*sy,
                rectangle.left+(x+w)*sx,rectangle.top+(y+h)*sy};
            if(destination.right<=clip.left||destination.left>=clip.right||destination.bottom<=clip.top||destination.top>=clip.bottom)continue;
            auto entry=std::find_if(tiles_.begin(),tiles_.end(),[&](const Tile& tile){return tile.target.Get()==&target&&tile.x==x&&tile.y==y;});
            if(entry==tiles_.end()){
                if(!converter){
                    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
                        ||FAILED(factory->CreateDecoderFromFilename(file_.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))
                        ||FAILED(decoder->GetFrame(0,&frame))||FAILED(factory->CreateFormatConverter(&converter))
                        ||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
                }
                std::vector<BYTE> pixels(static_cast<std::size_t>(w)*h*4);
                const WICRect source{static_cast<INT>(x),static_cast<INT>(y),static_cast<INT>(w),static_cast<INT>(h)};
                if(FAILED(converter->CopyPixels(&source,w*4,static_cast<UINT>(pixels.size()),pixels.data())))return false;
                ComPtr<ID2D1Bitmap> bitmap;
                if(FAILED(target.CreateBitmap(D2D1::SizeU(w,h),pixels.data(),w*4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&bitmap)))return false;
                entry=std::min_element(tiles_.begin(),tiles_.end(),[](const Tile& a,const Tile& b){return a.used<b.used;});
                *entry={&target,std::move(bitmap),x,y,stamp};
            }
            entry->used=stamp;target.DrawBitmap(entry->bitmap.Get(),destination,opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        return true;
    }
    struct Device {Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;};
    struct Tile {Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        UINT x{},y{};std::size_t used{};};
    std::vector<BYTE> pixels_;
    UINT width_{},height_{};
    UINT sourceWidth_{},sourceHeight_{};
    std::filesystem::path file_;
    mutable std::array<Tile,64> tiles_{};
    mutable std::size_t stamp_{};
    mutable std::array<Device,2> devices_{};
    mutable std::size_t next_{};
};
}
