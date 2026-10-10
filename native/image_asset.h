#pragma once
#include "common.h"
#include <wincodec.h>
#include <wincrypt.h>
#include <filesystem>

namespace callayers {
inline constexpr size_t MaxImageBytes=20*1024*1024,MaxEmbeddedImageChars=8*1024*1024,MaxSceneImageChars=32*1024*1024,MaxDeckBytes=48*1024*1024;
struct ImageAsset {
    UINT width=0,height=0;
    std::vector<BYTE> pixels; // Premultiplied BGRA, shared by scene history.
    std::wstring png;
};
class ImageCodec {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> Decoder(const std::vector<BYTE>& bytes){
        if(bytes.empty()||bytes.size()>MaxImageBytes)throw std::runtime_error("Use an image smaller than 20 MB.");
        ComPtr<IWICStream> stream;Check(wic->CreateStream(&stream));Check(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size())));
        ComPtr<IWICBitmapDecoder> decoder;Check(wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder));return decoder;
    }
    void Dimensions(IWICBitmapSource* source,UINT& w,UINT& h){Check(source->GetSize(&w,&h));if(!w||!h||w>32768||h>32768||uint64_t(w)*h>40000000)throw std::runtime_error("Use an image with at most 40 million pixels.");}
    void Pixels(IWICBitmapSource* source,ImageAsset& result){
        Dimensions(source,result.width,result.height);ComPtr<IWICFormatConverter> converter;Check(wic->CreateFormatConverter(&converter));
        Check(converter->Initialize(source,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        result.pixels.resize(size_t(result.width)*result.height*4);Check(converter->CopyPixels(nullptr,result.width*4,static_cast<UINT>(result.pixels.size()),result.pixels.data()));
    }
public:
    ImageCodec(){Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));}
    std::shared_ptr<const ImageAsset> Import(const std::vector<BYTE>& bytes){
        auto decoder=Decoder(bytes);ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(0,&frame));UINT w,h;Dimensions(frame.Get(),w,h);
        ComPtr<IWICBitmapSource> source;Check(frame.As(&source));
        ComPtr<IWICMetadataQueryReader> metadata;PROPVARIANT orientation{};
        if(SUCCEEDED(frame->GetMetadataQueryReader(&metadata))&&SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&orientation))){
            if(orientation.vt==VT_UI2&&orientation.uiVal>=2&&orientation.uiVal<=8){
                const WICBitmapTransformOptions transforms[]{WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,
                    static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,
                    static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
                ComPtr<IWICBitmapFlipRotator> rotate;Check(wic->CreateBitmapFlipRotator(&rotate));Check(rotate->Initialize(source.Get(),transforms[orientation.uiVal-1]));Check(rotate.As(&source));
            }
        }PropVariantClear(&orientation);Dimensions(source.Get(),w,h);
        if(std::max(w,h)>2048){double scale=2048./std::max(w,h);ComPtr<IWICBitmapScaler> scaler;Check(wic->CreateBitmapScaler(&scaler));Check(scaler->Initialize(source.Get(),std::max(1u,static_cast<UINT>(w*scale)),std::max(1u,static_cast<UINT>(h*scale)),WICBitmapInterpolationModeFant));Check(scaler.As(&source));}
        auto result=std::make_shared<ImageAsset>();Pixels(source.Get(),*result);
        ComPtr<IStream> stream;Check(CreateStreamOnHGlobal(nullptr,TRUE,&stream));ComPtr<IWICBitmapEncoder> encoder;Check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> output;Check(encoder->CreateNewFrame(&output,nullptr));Check(output->Initialize(nullptr));Check(output->SetSize(result->width,result->height));auto format=GUID_WICPixelFormat32bppBGRA;Check(output->SetPixelFormat(&format));Check(output->WriteSource(source.Get(),nullptr));Check(output->Commit());Check(encoder->Commit());
        STATSTG stat{};Check(stream->Stat(&stat,STATFLAG_NONAME));if(stat.cbSize.QuadPart>6*1024*1024)throw std::runtime_error("This image is too detailed to embed. Try a smaller image.");
        HGLOBAL memory;Check(GetHGlobalFromStream(stream.Get(),&memory));auto data=static_cast<BYTE*>(GlobalLock(memory));if(!data)throw E_OUTOFMEMORY;
        DWORD chars=0;bool ok=CryptBinaryToStringW(data,static_cast<DWORD>(stat.cbSize.QuadPart),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&chars)!=FALSE;
        result->png.resize(chars);if(ok)ok=CryptBinaryToStringW(data,static_cast<DWORD>(stat.cbSize.QuadPart),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,result->png.data(),&chars)!=FALSE;GlobalUnlock(memory);Check(ok?S_OK:E_FAIL);
        result->png.resize(chars);while(!result->png.empty()&&result->png.back()==L'\0')result->png.pop_back();return result;
    }
    std::shared_ptr<const ImageAsset> LoadEmbedded(const std::wstring& png){
        if(png.empty()||png.size()>MaxEmbeddedImageChars)throw std::runtime_error("Invalid embedded image size.");
        DWORD count=0;if(!CryptStringToBinaryW(png.c_str(),static_cast<DWORD>(png.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&count,nullptr,nullptr))throw std::runtime_error("Invalid embedded image encoding.");
        std::vector<BYTE> bytes(count);Check(CryptStringToBinaryW(png.c_str(),static_cast<DWORD>(png.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,bytes.data(),&count,nullptr,nullptr)?S_OK:E_FAIL);
        const BYTE signature[]{137,80,78,71,13,10,26,10};if(bytes.size()<8||memcmp(bytes.data(),signature,8))throw std::runtime_error("Embedded image must be PNG.");
        auto decoder=Decoder(bytes);ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(0,&frame));UINT w,h;Dimensions(frame.Get(),w,h);if(w>2048||h>2048)throw std::runtime_error("Embedded image dimensions exceed the limit.");
        auto result=std::make_shared<ImageAsset>();Pixels(frame.Get(),*result);result->png=png;return result;
    }
    std::shared_ptr<const ImageAsset> LoadFile(const std::filesystem::path& path){
        Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)throw std::runtime_error("Could not open the image file.");
        LARGE_INTEGER size;Check(GetFileSizeEx(file.value,&size)?S_OK:E_FAIL);if(size.QuadPart<=0||size.QuadPart>MaxImageBytes)throw std::runtime_error("Use an image smaller than 20 MB.");
        std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart));DWORD read;Check(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size()?S_OK:E_FAIL);return Import(bytes);
    }
    std::shared_ptr<const ImageAsset> FromDib(const BYTE* data,size_t size){
        if(size<sizeof(BITMAPINFOHEADER)||size>MaxImageBytes)throw std::runtime_error("Unsupported clipboard image.");
        BITMAPINFOHEADER header;memcpy(&header,data,sizeof(header));
        if((header.biSize!=40&&header.biSize!=108&&header.biSize!=124)||header.biSize>size||header.biPlanes!=1||
            (header.biCompression!=BI_RGB&&header.biCompression!=BI_BITFIELDS)||header.biClrUsed>256)throw std::runtime_error("Unsupported clipboard bitmap format.");
        size_t colors=header.biClrUsed?header.biClrUsed:header.biBitCount<=8?size_t(1)<<header.biBitCount:0;
        size_t offset=header.biSize+colors*4+(header.biSize==40&&header.biCompression==BI_BITFIELDS?12:0);if(offset>=size)throw std::runtime_error("Invalid clipboard bitmap.");
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfSize=static_cast<DWORD>(sizeof(file)+size);file.bfOffBits=static_cast<DWORD>(sizeof(file)+offset);
        std::vector<BYTE> bytes(sizeof(file)+size);memcpy(bytes.data(),&file,sizeof(file));memcpy(bytes.data()+sizeof(file),data,size);return Import(bytes);
    }
};
}
