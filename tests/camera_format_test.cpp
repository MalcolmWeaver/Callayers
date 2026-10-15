#include <windows.h>
#include <dshow.h>
#include <wrl.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
int main(){try{
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    auto library=LoadLibraryW((std::filesystem::path(exe).parent_path()/L"CallayersCamera.dll").c_str());
    require(library!=nullptr,"Camera DLL could not load");
    auto factory=reinterpret_cast<HRESULT(__stdcall*)(REFCLSID,REFIID,void**)>(GetProcAddress(library,"DllGetClassObject"));
    require(factory!=nullptr,"Camera factory missing");
    const CLSID camera={0x49c2dca8,0xf030,0x4c75,{0x9b,0x0e,0xc5,0x1e,0x33,0xc1,0xa2,0x91}};
    {
        ComPtr<IClassFactory> creator;require(SUCCEEDED(factory(camera,IID_PPV_ARGS(&creator))),"Camera factory failed");
        ComPtr<IBaseFilter> filter;require(SUCCEEDED(creator->CreateInstance(nullptr,IID_PPV_ARGS(&filter))),"Camera creation failed");
        ComPtr<IEnumPins> pins;require(SUCCEEDED(filter->EnumPins(&pins)),"Camera pins missing");
        ComPtr<IPin> pin;require(pins->Next(1,&pin,nullptr)==S_OK,"Camera output missing");
        ComPtr<IAMStreamConfig> config;require(SUCCEEDED(pin.As(&config)),"Camera stream configuration missing");
        int count=0,size=0;require(SUCCEEDED(config->GetNumberOfCapabilities(&count,&size)) && count==2 && size==sizeof(VIDEO_STREAM_CONFIG_CAPS),"Camera formats missing");
        for(int i=0;i<count;++i){
            AM_MEDIA_TYPE* type=nullptr;VIDEO_STREAM_CONFIG_CAPS caps{};
            require(SUCCEEDED(config->GetStreamCaps(i,&type,reinterpret_cast<BYTE*>(&caps))) && type,"Camera format unavailable");
            auto info=reinterpret_cast<VIDEOINFOHEADER*>(type->pbFormat);
            bool valid=type->formattype==FORMAT_VideoInfo && type->cbFormat>=sizeof(VIDEOINFOHEADER) &&
                info->bmiHeader.biWidth==1920 && info->bmiHeader.biHeight==1080 && info->AvgTimePerFrame==10000000/30 &&
                caps.MinOutputSize.cx==1920 && caps.MinOutputSize.cy==1080 && caps.MaxOutputSize.cx==1920 && caps.MaxOutputSize.cy==1080;
            bool accepted=SUCCEEDED(config->SetFormat(type));
            CoTaskMemFree(type->pbFormat);if(type->pUnk)type->pUnk->Release();CoTaskMemFree(type);
            require(valid && accepted,"Camera did not offer and accept 1080p/30");
        }
    }
    FreeLibrary(library);CoUninitialize();
    std::cout<<"Both virtual camera formats offer and accept 1920x1080 at 30fps\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
