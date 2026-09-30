#pragma once
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <array>

namespace noven::ui {
// 本地全尺寸图片，不复用会缩成物品图标的解码器；无网络或磁盘缓存。
// Local full-size image, not the item-thumbnail decoder; no network or disk cache.
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
        ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
            ||FAILED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))
            ||FAILED(decoder->GetFrame(0,&frame)))return false;
        UINT width=0,height=0;
        if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>4096||height>4096
            ||FAILED(factory->CreateFormatConverter(&converter))
            ||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
        std::vector<BYTE> pixels(static_cast<std::size_t>(width)*height*4);
        if(FAILED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data())))return false;
        pixels_=std::move(pixels);width_=width;height_=height;devices_={};return true;
    }
    bool Ready() const noexcept{return !pixels_.empty();}
    bool Draw(ID2D1RenderTarget& target,D2D1_RECT_F rectangle,float opacity=1) const {
        if(!Ready())return false;
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
    struct Device {Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;};
    std::vector<BYTE> pixels_;
    UINT width_{},height_{};
    mutable std::array<Device,2> devices_{};
    mutable std::size_t next_{};
};
}
