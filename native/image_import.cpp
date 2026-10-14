#include "image_import.h"
#include "auth_protocol.h"
#include <windows.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <winrt/base.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace callayers {
GeneratedCard ImportImage(const std::filesystem::path& path){
    auto extension=path.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),towlower);
    if(extension!=L".png" && extension!=L".jpg" && extension!=L".jpeg" && extension!=L".bmp")throw std::runtime_error("Choose a PNG, JPEG or BMP image.");
    if(std::filesystem::file_size(path)>32*1024*1024)throw std::runtime_error("Choose an image smaller than 32 MB.");
    winrt::com_ptr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())));
    winrt::com_ptr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.put()));
    winrt::com_ptr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,frame.put()));
    UINT width=0,height=0;winrt::check_hresult(frame->GetSize(&width,&height));
    if(!width || !height || uint64_t(width)*height>24000000)throw std::runtime_error("Choose an image with at most 24 million pixels.");
    double scale=std::min({1.0,1280.0/width,720.0/height});UINT w=std::max(1u,UINT(std::lround(width*scale))),h=std::max(1u,UINT(std::lround(height*scale)));
    winrt::com_ptr<IStream> stream;STATSTG info{};
    for(;;){
    winrt::com_ptr<IWICBitmapScaler> scaler;winrt::check_hresult(factory->CreateBitmapScaler(scaler.put()));winrt::check_hresult(scaler->Initialize(frame.get(),w,h,WICBitmapInterpolationModeFant));
    winrt::com_ptr<IWICFormatConverter> converter;winrt::check_hresult(factory->CreateFormatConverter(converter.put()));winrt::check_hresult(converter->Initialize(scaler.get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    stream=nullptr;winrt::check_hresult(CreateStreamOnHGlobal(nullptr,TRUE,stream.put()));
    winrt::com_ptr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put()));winrt::check_hresult(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> output;winrt::check_hresult(encoder->CreateNewFrame(output.put(),nullptr));winrt::check_hresult(output->Initialize(nullptr));winrt::check_hresult(output->SetSize(w,h));
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(output->SetPixelFormat(&format));winrt::check_hresult(output->WriteSource(converter.get(),nullptr));winrt::check_hresult(output->Commit());winrt::check_hresult(encoder->Commit());
    winrt::check_hresult(stream->Stat(&info,STATFLAG_NONAME));
    if(info.cbSize.QuadPart<=1024*1024)break;
    double smaller=.9*std::sqrt(1048576.0/info.cbSize.QuadPart);w=std::max(1u,UINT(w*smaller));h=std::max(1u,UINT(h*smaller));
    }
    LARGE_INTEGER start{};winrt::check_hresult(stream->Seek(start,STREAM_SEEK_SET,nullptr));std::vector<BYTE> bytes(size_t(info.cbSize.QuadPart));ULONG read=0;winrt::check_hresult(stream->Read(bytes.data(),ULONG(bytes.size()),&read));if(read!=bytes.size())throw std::runtime_error("Image read failed");
    DWORD length=0;if(!CryptBinaryToStringW(bytes.data(),DWORD(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&length))winrt::throw_last_error();
    std::wstring base64(length,L'\0');if(!CryptBinaryToStringW(bytes.data(),DWORD(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,base64.data(),&length))winrt::throw_last_error();base64.resize(length);
    GeneratedCard card;card.id=winrt::to_hstring(AuthRandomToken(24));card.name=path.filename().wstring();if(card.name.size()>80)card.name.resize(80);
    card.image=L"data:image/png;base64,"+base64;card.width=w;card.height=h;
    double display=std::min({1.0,480.0/w,340.0/h});card.w=w*display;card.h=h*display;
    if(!ValidGeneratedCard(card))throw std::runtime_error("This image could not be imported.");return card;
}
}
