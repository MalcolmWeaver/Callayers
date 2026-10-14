#include "image_import.h"
#include <windows.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <winrt/base.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace callayers;
void check(bool value){if(!value)throw std::runtime_error("Image import assertion failed");}
int main(){std::filesystem::path path;try{winrt::init_apartment();
    wchar_t temporary[MAX_PATH]{};check(GetTempPathW(MAX_PATH,temporary)!=0);path=std::filesystem::path(temporary)/L"callayers-image-import-test.png";
    // Transparent 1x1 PNG, independent of the source encoder used by import.
    const wchar_t* png=L"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DAAAAEAQEARwbK3gAAAABJRU5ErkJggg==";
    DWORD size=0;check(CryptStringToBinaryW(png,0,CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr));std::vector<BYTE> bytes(size);check(CryptStringToBinaryW(png,0,CRYPT_STRING_BASE64,bytes.data(),&size,nullptr,nullptr));
    {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(bytes.data()),bytes.size());}
    auto card=ImportImage(path);check(card.width==1 && card.height==1 && !card.visible && card.name==path.filename().wstring() && ValidGeneratedCard(card));
    std::filesystem::remove(path);std::vector<GeneratedCard> restored;check(ParseGeneratedCards(GeneratedCardsJson({card}),restored));check(restored[0].image==card.image && CardDocument(restored[0]).find(card.image)!=std::wstring::npos);
    // Check the saved PNG decodes after the original file has gone away.
    auto encoded=restored[0].image.substr(22);check(CryptStringToBinaryW(encoded.c_str(),DWORD(encoded.size()),CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr));bytes.resize(size);check(CryptStringToBinaryW(encoded.c_str(),DWORD(encoded.size()),CRYPT_STRING_BASE64,bytes.data(),&size,nullptr,nullptr));
    winrt::com_ptr<IStream> stream;winrt::check_hresult(CreateStreamOnHGlobal(nullptr,TRUE,stream.put()));ULONG written=0;winrt::check_hresult(stream->Write(bytes.data(),DWORD(bytes.size()),&written));LARGE_INTEGER zero{};winrt::check_hresult(stream->Seek(zero,STREAM_SEEK_SET,nullptr));
    winrt::com_ptr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())));winrt::com_ptr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromStream(stream.get(),nullptr,WICDecodeMetadataCacheOnLoad,decoder.put()));winrt::com_ptr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,frame.put()));UINT w=0,h=0;winrt::check_hresult(frame->GetSize(&w,&h));check(w==1 && h==1);
    winrt::com_ptr<IWICFormatConverter> converter;winrt::check_hresult(factory->CreateFormatConverter(converter.put()));winrt::check_hresult(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));BYTE pixel[4]{};winrt::check_hresult(converter->CopyPixels(nullptr,4,4,pixel));check(pixel[3]==0);
    auto invalid=card;invalid.image+=L"\" onerror=\"bad";check(!ValidGeneratedCard(invalid));invalid=card;invalid.html=L"<script>bad</script>";check(!ValidGeneratedCard(invalid));invalid=card;invalid.image=L"data:image/png;base64,"+std::wstring(1400000,L'A');check(!ValidGeneratedCard(invalid));
    path.replace_extension(L".bmp");BITMAPFILEHEADER header{0x4d42,54+1280*720*3,0,0,54};BITMAPINFOHEADER bitmap{};bitmap.biSize=40;bitmap.biWidth=1280;bitmap.biHeight=720;bitmap.biPlanes=1;bitmap.biBitCount=24;
    bytes.resize(1280*720*3);unsigned random=12345;for(auto& byte:bytes){random^=random<<13;random^=random>>17;random^=random<<5;byte=BYTE(random);}
    {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(&header),sizeof(header));file.write(reinterpret_cast<char*>(&bitmap),sizeof(bitmap));file.write(reinterpret_cast<char*>(bytes.data()),bytes.size());}
    auto detailed=ImportImage(path);std::filesystem::remove(path);check(detailed.width<1280 && detailed.height<720 && detailed.image.size()<=1400000 && ValidGeneratedCard(detailed));
    auto broken=path;{std::ofstream file(broken);file<<"not an image";}bool rejected=false;try{ImportImage(broken);}catch(...){rejected=true;}std::filesystem::remove(broken);check(rejected);
    std::cout<<"Image import, independent saved pixels, roundtrip and invalid input passed\n";return 0;
}catch(...){if(!path.empty())std::filesystem::remove(path);std::cerr<<"Image import checks failed\n";return 1;}}
