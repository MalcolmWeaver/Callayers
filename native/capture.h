#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace callayers {
struct CameraDevice { std::wstring name, identity; };
struct CameraImage { std::vector<uint8_t> pixels; UINT width = 0, height = 0; ULONGLONG tick = 0; };
struct CaptureState {
    std::mutex mutex;
    std::shared_ptr<const CameraImage> latest;
    std::atomic<HRESULT> error{S_OK};
    std::atomic<bool> running{false};
};
std::vector<CameraDevice> EnumerateCameras();
class Capture {
    Microsoft::WRL::ComPtr<IMFMediaSource> source_;
    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
    Microsoft::WRL::ComPtr<IMFSourceReaderCallback> callback_;
public:
    std::shared_ptr<CaptureState> state = std::make_shared<CaptureState>();
    ~Capture() { stop(); }
    HRESULT start(const std::wstring& identity);
    void stop();
};
}
