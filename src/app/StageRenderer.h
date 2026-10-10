#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "../shared/FrameProtocol.h"

namespace callayers {

class StageRenderer {
public:
    using StatusCallback = std::function<void(std::wstring)>;

    void Start(
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel,
        winrt::Microsoft::UI::Xaml::Controls::Image const& preview,
        StatusCallback status);
    void Stop();

private:
    bool CreateDevice(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
    void Render();

    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
    winrt::com_ptr<ID3D11Device> m_device;
    winrt::com_ptr<ID3D11DeviceContext> m_context;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11Texture2D> m_frame;
    winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
    std::vector<uint8_t> m_bgra;
    std::vector<uint8_t> m_nv12;
    MappedFrames m_frames;
    std::shared_ptr<std::atomic<bool>> m_alive;
    bool m_published = false;
};

} // namespace callayers
