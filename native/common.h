#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mfvirtualcamera.h>
#include <wrl.h>
#include <sddl.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace callayers {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
inline constexpr UINT Width = 1920, Height = 1080, Fps = 30;
inline constexpr DWORD FrameBytes = Width * Height * 3 / 2;
inline constexpr LONGLONG FrameDuration = 10'000'000 / Fps;
inline constexpr DWORD VideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
inline constexpr DWORD AllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
inline constexpr CLSID CameraClsid = {0xa487a33b,0x6ec3,0x43f1,{0xa8,0x94,0xca,0x2d,0x93,0x76,0x19,0xf0}};
inline constexpr wchar_t CameraClsidString[] = L"{A487A33B-6EC3-43F1-A894-CA2D937619F0}";
inline constexpr wchar_t PipeName[] = L"\\\\.\\pipe\\Callayers.Step1.Video.v1";
inline constexpr wchar_t CameraName[] = L"Callayers";

inline void Check(HRESULT hr) { if (FAILED(hr)) throw hr; }
inline std::wstring ErrorText(HRESULT hr) {
    wchar_t code[32]; swprintf_s(code, L"0x%08X", static_cast<unsigned>(hr));
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, hr, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = code;
    if (message) { result += L" — "; result += message; LocalFree(message); }
    return result;
}
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HANDLE h = nullptr) { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = h; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct FrameHeader {
    uint32_t magic = 0x43414C31;
    uint32_t bytes = FrameBytes;
    uint32_t width = Width;
    uint32_t height = Height;
    uint64_t sequence = 0;
    uint64_t capturedMs = 0;
    uint32_t mode = 0; // 0 idle, 1 moving pattern, 2 webcam
    uint32_t reserved = 0;
};
static_assert(sizeof(FrameHeader) == 40);
struct Frame {
    FrameHeader header;
    std::vector<BYTE> pixels = std::vector<BYTE>(FrameBytes);
};
inline bool Valid(const FrameHeader& h) {
    return h.magic == 0x43414C31 && h.bytes == FrameBytes && h.width == Width && h.height == Height && h.mode <= 2;
}
inline BYTE Clamp(int v) { return static_cast<BYTE>(std::clamp(v, 0, 255)); }
inline std::shared_ptr<Frame> Pattern(uint64_t sequence, bool idle = false) {
    auto f = std::make_shared<Frame>();
    f->header.sequence = sequence; f->header.capturedMs = GetTickCount64(); f->header.mode = idle ? 0 : 1;
    const BYTE bars[] = {180,162,131,112,84,65,35,16};
    for (UINT y=0; y<Height; ++y) {
        BYTE* row = f->pixels.data() + y*Width;
        if (idle) std::memset(row, 30, Width);
        else for (UINT x=0; x<Width; ++x) row[x] = bars[x/(Width/8)];
    }
    std::fill(f->pixels.begin()+Width*Height, f->pixels.end(), BYTE{128});
    // A stationary pause symbol makes loss of the producer visibly different from live video.
    const UINT left = idle ? Width/2-90 : static_cast<UINT>((sequence*12)%(Width-100));
    for (UINT y=Height/3; y<Height*2/3; ++y) {
        std::memset(f->pixels.data()+y*Width+left, idle ? 160 : 235, idle ? 55 : 100);
        if (idle) std::memset(f->pixels.data()+y*Width+Width/2+35, 160, 55);
    }
    return f;
}
inline void ToBgra(const Frame& f, std::vector<BYTE>& rgb) {
    rgb.resize(Width*Height*4);
    for (UINT y=0; y<Height; ++y) for (UINT x=0; x<Width; ++x) {
        int c = std::max(0, int(f.pixels[y*Width+x])-16);
        const UINT uv = Width*Height + (y/2)*Width + (x & ~1u);
        int d = int(f.pixels[uv])-128, e = int(f.pixels[uv+1])-128;
        BYTE* p = rgb.data()+(y*Width+x)*4;
        p[0]=Clamp((298*c+541*d+128)>>8); p[1]=Clamp((298*c-55*d-136*e+128)>>8);
        p[2]=Clamp((298*c+459*e+128)>>8); p[3]=255;
    }
}
inline std::shared_ptr<Frame> FromBgra(const BYTE* rgb, LONG stride, UINT w, UINT h, uint64_t sequence) {
    auto f = Pattern(sequence, true); f->header.mode=2;
    // Preserve aspect ratio and letterbox; never stretch the user's camera.
    const double scale = std::min(double(Width)/w, double(Height)/h);
    UINT dw = std::max(2u, (static_cast<UINT>(w*scale)&~1u));
    UINT dh = std::max(2u, (static_cast<UINT>(h*scale)&~1u));
    UINT ox = ((Width-dw)/2)&~1u, oy=((Height-dh)/2)&~1u;
    std::fill(f->pixels.begin(), f->pixels.begin()+Width*Height, BYTE{16});
    for (UINT y=0; y<dh; y+=2) for (UINT x=0; x<dw; x+=2) {
        int sumU=0, sumV=0;
        for (UINT dy=0; dy<2; ++dy) for (UINT dx=0; dx<2; ++dx) {
            const UINT sx=(x+dx)*w/dw, sy=(y+dy)*h/dh;
            const BYTE* p=rgb+static_cast<ptrdiff_t>(sy)*stride+sx*4;
            int b=p[0], g=p[1], r=p[2];
            f->pixels[(oy+y+dy)*Width+ox+x+dx]=Clamp(((47*r+157*g+16*b+128)>>8)+16);
            sumU+=((-26*r-87*g+112*b+128)>>8)+128;
            sumV+=((112*r-102*g-10*b+128)>>8)+128;
        }
        UINT uv=Width*Height+((oy+y)/2)*Width+ox+x;
        f->pixels[uv]=Clamp(sumU/4); f->pixels[uv+1]=Clamp(sumV/4);
    }
    return f;
}
inline ComPtr<IMFMediaType> VideoType() {
    ComPtr<IMFMediaType> type; Check(MFCreateMediaType(&type));
    Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    Check(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    Check(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, Width, Height));
    Check(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, Fps, 1));
    Check(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    Check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    Check(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
    Check(type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE));
    Check(type->SetUINT32(MF_MT_SAMPLE_SIZE, FrameBytes));
    Check(type->SetUINT32(MF_MT_DEFAULT_STRIDE, Width));
    Check(type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709));
    Check(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
    return type;
}
inline bool Transfer(HANDLE pipe, bool write, void* data, DWORD size, HANDLE stop, DWORD timeout = 100) {
    BYTE* cursor=static_cast<BYTE*>(data);
    while (size) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) return false;
        OVERLAPPED ov{}; ov.hEvent=event.value; DWORD n=0;
        BOOL ok=write ? WriteFile(pipe,cursor,size,&n,&ov) : ReadFile(pipe,cursor,size,&n,&ov);
        if (!ok) {
            if (GetLastError()!=ERROR_IO_PENDING) return false;
            HANDLE waits[]={event.value,stop};
            DWORD result=WaitForMultipleObjects(stop?2:1,waits,FALSE,timeout);
            if (result!=WAIT_OBJECT_0) { CancelIoEx(pipe,&ov); GetOverlappedResult(pipe,&ov,&n,TRUE); return false; }
            if (!GetOverlappedResult(pipe,&ov,&n,FALSE)) return false;
        }
        if (!n) return false;
        cursor+=n; size-=n;
    }
    return true;
}
inline std::shared_ptr<Frame> ReadFrame(HANDLE stop=nullptr) {
    Handle pipe(CreateFileW(PipeName,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
    if (!pipe) return {};
    auto f=std::make_shared<Frame>();
    if (!Transfer(pipe.value,false,&f->header,sizeof(FrameHeader),stop) || !Valid(f->header)) return {};
    if (!Transfer(pipe.value,false,f->pixels.data(),FrameBytes,stop)) return {};
    BYTE ack=1; if(!Transfer(pipe.value,true,&ack,1,stop))return {};
    if (GetTickCount64()-f->header.capturedMs>750) return {};
    return f;
}
struct Device { std::wstring name, link; };
inline uint64_t CameraModeScore(UINT width,UINT height,UINT numerator,UINT denominator) {
    if(!width||!height||!denominator||width>Width||height>Height||
       uint64_t(numerator)<15ull*denominator||uint64_t(numerator)>60ull*denominator)return 0;
    // Prefer a camera's 30 fps mode at any supported size over its 60 fps mode.
    const bool withinOutputRate=uint64_t(numerator)<=uint64_t(Fps)*denominator;
    const auto rate=std::min(numerator/denominator,60u);
    return (withinOutputRate?1'000'000'000ull:0ull)+uint64_t(width)*height*100+
        (withinOutputRate?rate:60u-rate);
}
inline std::vector<Device> Cameras(bool includeCallayers=false) {
    ComPtr<IMFAttributes> a; Check(MFCreateAttributes(&a,1));
    Check(a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    IMFActivate** devices=nullptr; UINT count=0;
    Check(MFEnumDeviceSources(a.Get(),&devices,&count));
    std::vector<Device> result;
    for (UINT i=0;i<count;++i) {
        wchar_t *name=nullptr,*link=nullptr; UINT length=0;
        devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&length);
        devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,&link,&length);
        if (name && link && (includeCallayers || !wcsstr(name,L"Callayers"))) result.push_back({name,link});
        CoTaskMemFree(name); CoTaskMemFree(link); devices[i]->Release();
    }
    CoTaskMemFree(devices); return result;
}
inline ComPtr<IMFMediaSource> OpenCamera(const Device& device) {
    ComPtr<IMFAttributes> a; Check(MFCreateAttributes(&a,2));
    Check(a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    Check(a->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,device.link.c_str()));
    ComPtr<IMFMediaSource> source; Check(MFCreateDeviceSource(a.Get(),&source)); return source;
}
inline ComPtr<IMFVirtualCamera> VirtualCamera() {
    ComPtr<IMFVirtualCamera> camera;
    Check(MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,MFVirtualCameraLifetime_System,
        MFVirtualCameraAccess_CurrentUser,CameraName,CameraClsidString,nullptr,0,&camera));
    return camera;
}
}
