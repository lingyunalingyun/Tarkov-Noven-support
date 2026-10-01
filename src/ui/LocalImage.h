#pragma once
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <array>
#include <algorithm>
#include <optional>
#include <fstream>
#include <cstdint>
#include <cstring>

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
        sourceWidth_=width;sourceHeight_=height;file_=file;devices_={};tiles_={};tileIndex_.clear();
        auto pack=file;pack.replace_extension(L".tiles");
        if(std::filesystem::exists(pack)){
            std::ifstream input(pack,std::ios::binary);char magic[8]{};std::array<std::uint32_t,4> header{};
            input.read(magic,8);input.read(reinterpret_cast<char*>(header.data()),sizeof(header));
            if(!input||std::memcmp(magic,"NVTILES1",8)!=0||!header[0]||!header[1]
                ||header[0]>8192||header[1]>8192||header[2]!=512
                ||header[3]!=((header[0]+511)/512)*((header[1]+511)/512))return false;
            std::vector<TileSource> index(header[3]);input.read(reinterpret_cast<char*>(index.data()),static_cast<std::streamsize>(index.size()*sizeof(TileSource)));
            const auto length=std::filesystem::file_size(pack);
            for(const auto& tile:index)if(tile.offset<24+index.size()*8||!tile.length||tile.length>2*1024*1024
                ||static_cast<std::uint64_t>(tile.offset)+tile.length>length)return false;
            if(!input)return false;
            tileIndex_=std::move(index);file_=pack;sourceWidth_=header[0];sourceHeight_=header[1];
        }
        return true;
    }
    bool Ready() const noexcept{return !pixels_.empty();}
    // UI 所有者可释放闲置高清 GPU 块；保留预览、包目录和 target 预览身份，下次按需重读。
    // UI owners may release idle detail GPU tiles, retaining previews/index/preview targets for on-demand reload.
    void ReleaseDetailCache() const noexcept{tiles_={};}
    std::size_t DetailTileCount() const noexcept{return static_cast<std::size_t>(std::count_if(tiles_.begin(),tiles_.end(),[](const Tile& tile){return tile.bitmap!=nullptr;}));}
    bool Draw(ID2D1RenderTarget& target,D2D1_RECT_F rectangle,float opacity=1,
        std::optional<D2D1_RECT_F> clip=std::nullopt) const {
        if(!Ready())return false;
        float dpiX=96,dpiY=96;target.GetDpi(&dpiX,&dpiY);
        if(!tileIndex_.empty()&&sourceWidth_>width_&&((rectangle.right-rectangle.left)*dpiX/96>width_*1.5F
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
        ComPtr<IWICImagingFactory> factory;
        std::ifstream pack;
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
                if(tileIndex_.empty())return false;
                if(!factory&&FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return false;
                if(!pack.is_open())pack.open(file_,std::ios::binary);
                const auto& source=tileIndex_[(y/tileSize)*((sourceWidth_+tileSize-1)/tileSize)+x/tileSize];
                std::vector<BYTE> encoded(source.length);pack.seekg(source.offset);
                pack.read(reinterpret_cast<char*>(encoded.data()),source.length);if(!pack)return false;
                ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;
                ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
                if(FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(encoded.data(),source.length))
                    ||FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))
                    ||FAILED(decoder->GetFrame(0,&frame))||FAILED(factory->CreateFormatConverter(&converter))
                    ||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
                UINT decodedWidth=0,decodedHeight=0;
                if(FAILED(frame->GetSize(&decodedWidth,&decodedHeight))||decodedWidth!=w||decodedHeight!=h)return false;
                std::vector<BYTE> pixels(static_cast<std::size_t>(w)*h*4);
                if(FAILED(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data())))return false;
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
    // 固定 little-endian 的包目录，块内 PNG 独立压缩；磁盘读取不会触碰其他区域。
    // Fixed little-endian directory with independently compressed PNGs; reads touch only requested tiles.
    struct TileSource {std::uint32_t offset{},length{};};
    static_assert(sizeof(TileSource)==8);
    std::vector<TileSource> tileIndex_;
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
