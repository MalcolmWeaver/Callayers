#include <windows.h>

#include "../shared/FrameProtocol.h"

#include <cstdlib>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mfvirtualcamera.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfsensorgroup.lib")
#pragma comment(lib, "ole32.lib")

namespace {

int PeakColumn(const uint8_t* yPlane) {
    int bestX = 0;
    int best = -1;
    for (int x = 0; x < static_cast<int>(callayers::kWidth); x += 4) {
        int sum = 0;
        for (int y = 0; y < static_cast<int>(callayers::kHeight); y += 20) {
            sum += yPlane[y * callayers::kWidth + x];
        }
        if (sum > best) {
            best = sum;
            bestX = x;
        }
    }
    return bestX;
}

HRESULT ReadPeak(IMFSourceReader* reader, int* peak) {
    IMFSample* sample = nullptr;
    DWORD flags = 0;
    DWORD stream = 0;
    LONGLONG time = 0;
    HRESULT hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags, &time, &sample);
    if (FAILED(hr)) {
        return hr;
    }
    if (!sample || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
        if (sample) {
            sample->Release();
        }
        return E_FAIL;
    }
    IMFMediaBuffer* buffer = nullptr;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    BYTE* data = nullptr;
    DWORD length = 0;
    if (SUCCEEDED(hr)) {
        hr = buffer->Lock(&data, nullptr, &length);
    }
    if (SUCCEEDED(hr)) {
        if (length < callayers::kWidth * callayers::kHeight) {
            hr = E_FAIL;
        } else {
            *peak = PeakColumn(data);
        }
        buffer->Unlock();
    }
    if (buffer) {
        buffer->Release();
    }
    sample->Release();
    return hr;
}

HRESULT OpenReader(IMFMediaSource* source, IMFSourceReader** readerOut) {
    HRESULT hr = MFCreateSourceReaderFromMediaSource(source, nullptr, readerOut);
    if (FAILED(hr)) {
        return hr;
    }
    IMFMediaType* type = nullptr;
    hr = MFCreateMediaType(&type);
    if (FAILED(hr)) {
        return hr;
    }
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(type, MF_MT_FRAME_SIZE, callayers::kWidth, callayers::kHeight);
    MFSetAttributeRatio(type, MF_MT_FRAME_RATE, callayers::kFps, 1);
    hr = (*readerOut)->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
    type->Release();
    return hr;
}

int DirectTest() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dll = exe;
    const auto slash = dll.find_last_of(L"\\/");
    dll = dll.substr(0, slash + 1) + L"CallayersVCam.dll";
    HMODULE library = LoadLibraryW(dll.c_str());
    if (!library) {
        std::wcerr << L"Could not load " << dll << L"\n";
        return 1;
    }
    auto registerServer = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(library, "DllRegisterServer"));
    if (!registerServer) {
        std::wcerr << L"DllRegisterServer missing\n";
        return 1;
    }
    const HRESULT registered = registerServer();
    std::wcout << L"Register " << std::hex << static_cast<unsigned>(registered) << std::dec << L"\n";

    IMFActivate* activate = nullptr;
    HRESULT hr = CoCreateInstance(callayers::kActivateClsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&activate));
    if (FAILED(hr)) {
        std::wcerr << L"CoCreate failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
        return 1;
    }
    IMFMediaSource* source = nullptr;
    hr = activate->ActivateObject(IID_IMFMediaSource, reinterpret_cast<void**>(&source));
    if (FAILED(hr)) {
        activate->Release();
        std::wcerr << L"ActivateObject failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
        return 1;
    }
    IMFSourceReader* reader = nullptr;
    hr = OpenReader(source, &reader);
    source->Release();
    if (FAILED(hr)) {
        activate->Release();
        std::wcerr << L"Reader failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
        return 1;
    }

    callayers::MappedFrames frames;
    if (!frames.OpenOrCreate()) {
        std::wcerr << L"Shared memory failed " << GetLastError() << L"\n";
        reader->Release();
        activate->Release();
        return 1;
    }
    std::vector<uint8_t> bgra(static_cast<size_t>(callayers::kWidth) * callayers::kHeight * 4);
    std::vector<uint8_t> nv12(callayers::kNv12Size);
    int first = -1;
    int last = -1;
    for (int i = 0; i < 6; ++i) {
        const int bar = 80 + i * 240;
        callayers::FillTestPatternBgra(bgra.data(), static_cast<int>(callayers::kWidth) * 4, bar);
        callayers::ConvertBgraToNv12(bgra.data(), static_cast<int>(callayers::kWidth) * 4, nv12.data());
        frames.Publish(nv12.data());
        int peak = -1;
        hr = ReadPeak(reader, &peak);
        if (FAILED(hr)) {
            std::wcerr << L"Read failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
            frames.Close();
            reader->Release();
            activate->Release();
            return 1;
        }
        std::wcout << L"live peak " << peak << L" expected " << bar << L"\n";
        if (first < 0) {
            first = peak;
        }
        last = peak;
    }
    frames.MarkClosed();
    int idleA = -1;
    int idleB = -1;
    if (FAILED(ReadPeak(reader, &idleA)) || FAILED(ReadPeak(reader, &idleB))) {
        std::wcerr << L"Idle read failed\n";
        frames.Close();
        reader->Release();
        activate->Release();
        return 1;
    }
    std::wcout << L"idle peaks " << idleA << L" " << idleB << L"\n";
    frames.Close();
    reader->Release();
    activate->Release();

    const bool moved = last - first > 100;
    const bool idleCentered = idleA > 700 && idleA < 1200 && std::abs(idleA - idleB) < 16;
    if (!moved || !idleCentered) {
        std::wcerr << L"FAIL direct frame path\n";
        return 1;
    }
    std::wcout << L"PASS direct frame path\n";
    return 0;
}

int CameraTest() {
    for (int attempt = 0; attempt < 20; ++attempt) {
        IMFAttributes* attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) {
            return 1;
        }
        attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        hr = MFEnumDeviceSources(attributes, &devices, &count);
        attributes->Release();
        if (FAILED(hr)) {
            std::wcerr << L"Enum failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
            return 1;
        }
        IMFActivate* match = nullptr;
        for (UINT32 i = 0; i < count; ++i) {
            wchar_t* name = nullptr;
            UINT32 length = 0;
            devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length);
            if (name) {
                std::wcout << L"device " << name << L"\n";
                if (wcscmp(name, callayers::kCameraName) == 0) {
                    match = devices[i];
                    match->AddRef();
                }
                CoTaskMemFree(name);
            }
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
        if (!match) {
            Sleep(500);
            continue;
        }
        IMFMediaSource* source = nullptr;
        hr = match->ActivateObject(IID_IMFMediaSource, reinterpret_cast<void**>(&source));
        match->Release();
        if (FAILED(hr)) {
            std::wcerr << L"Activate camera failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
            return 1;
        }
        IMFSourceReader* reader = nullptr;
        hr = OpenReader(source, &reader);
        source->Release();
        if (FAILED(hr)) {
            std::wcerr << L"Camera reader failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
            return 1;
        }
        int first = -1;
        int last = -1;
        for (int i = 0; i < 10; ++i) {
            int peak = -1;
            hr = ReadPeak(reader, &peak);
            if (FAILED(hr)) {
                std::wcerr << L"Camera read failed " << std::hex << static_cast<unsigned>(hr) << L"\n";
                reader->Release();
                return 1;
            }
            std::wcout << L"camera peak " << peak << L"\n";
            if (first < 0) {
                first = peak;
            }
            last = peak;
            Sleep(50);
        }
        reader->Release();
        if (std::abs(last - first) < 8) {
            std::wcerr << L"FAIL camera frame did not move\n";
            return 1;
        }
        std::wcout << L"PASS virtual camera\n";
        return 0;
    }
    std::wcerr << L"Callayers camera was not enumerated\n";
    return 1;
}

} // namespace

int CreateTest() {
    auto hex = [](HRESULT hr) {
        wchar_t text[16]{};
        swprintf_s(text, L"%08X", static_cast<unsigned>(hr));
        return std::wstring(text);
    };
    struct Attempt {
        const wchar_t* label;
        MFVirtualCameraLifetime life;
        const wchar_t* name;
        const wchar_t* source;
    };
    const Attempt attempts[] = {
        { L"system", MFVirtualCameraLifetime_System, callayers::kCameraName, callayers::kActivateClsidString },
        { L"session", MFVirtualCameraLifetime_Session, L"CallayersSession", callayers::kActivateClsidString },
        { L"bogus", MFVirtualCameraLifetime_System, L"CallayersBogus", L"{00000000-0000-0000-0000-000000000001}" },
    };
    int result = 1;
    for (const Attempt& attempt : attempts) {
        IMFVirtualCamera* camera = nullptr;
        const HRESULT created = MFCreateVirtualCamera(
            MFVirtualCameraType_SoftwareCameraSource,
            attempt.life,
            MFVirtualCameraAccess_CurrentUser,
            attempt.name,
            attempt.source,
            nullptr,
            0,
            &camera);
        std::wcout << attempt.label << L" create " << hex(created) << L"\n";
        if (FAILED(created) || !camera) {
            continue;
        }
        const HRESULT started = camera->Start(nullptr);
        std::wcout << attempt.label << L" start " << hex(started) << L"\n";
        if (FAILED(started)) {
            const HRESULT removed = camera->Remove();
            std::wcout << attempt.label << L" remove " << hex(removed) << L"\n";
        } else if (attempt.life == MFVirtualCameraLifetime_System && wcscmp(attempt.name, callayers::kCameraName) == 0) {
            result = 0;
        } else {
            camera->Stop();
            camera->Remove();
        }
        camera->Release();
    }
    return result;
}

int wmain(int argc, wchar_t** argv) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        return 1;
    }
    const HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(mf)) {
        CoUninitialize();
        return 1;
    }
    const bool direct = argc > 1 && wcscmp(argv[1], L"--direct") == 0;
    const bool create = argc > 1 && wcscmp(argv[1], L"--create") == 0;
    const int code = create ? CreateTest() : direct ? DirectTest() : CameraTest();
    MFShutdown();
    CoUninitialize();
    return code;
}
