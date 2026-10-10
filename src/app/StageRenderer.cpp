#include "pch.h"
#include "StageRenderer.h"
#include "VirtualCamera.h"

#include <cstring>
#include <thread>

#include <microsoft.ui.xaml.media.dxinterop.h>

namespace callayers {

void StageRenderer::Start(
    winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel,
    winrt::Microsoft::UI::Xaml::Controls::Image const& preview,
    StatusCallback status) {
    m_alive = std::make_shared<std::atomic<bool>>(true);
    m_bgra.resize(static_cast<size_t>(kWidth) * kHeight * 4);
    m_nv12.resize(kNv12Size);
    m_bitmap = winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap(kWidth, kHeight);
    preview.Source(m_bitmap);
    CreateDevice(panel);

    auto alive = m_alive;
    auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    std::thread([alive, queue, status = std::move(status)] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const std::wstring text = EnsureVirtualCamera();
        if (alive->load()) {
            queue.TryEnqueue([alive, status, text] {
                if (alive->load()) {
                    status(text);
                }
            });
        }
        while (alive->load()) {
            Sleep(200);
        }
        CoUninitialize();
    }).detach();

    m_timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    m_timer.Interval(std::chrono::milliseconds(33));
    m_timer.IsRepeating(true);
    m_timer.Tick([this](auto&&, auto&&) { Render(); });
    m_timer.Start();
    Render();
}

void StageRenderer::Stop() {
    if (m_alive) {
        m_alive->store(false);
    }
    if (m_timer) {
        m_timer.Stop();
        m_timer = nullptr;
    }
    m_frames.MarkClosed();
    m_frames.Close();
    m_frame = nullptr;
    m_swapChain = nullptr;
    m_context = nullptr;
    m_device = nullptr;
}

bool StageRenderer::CreateDevice(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        m_device.put(),
        &level,
        m_context.put());
    if (FAILED(hr)) {
        return false;
    }

    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    hr = m_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()));
    if (FAILED(hr)) {
        return false;
    }
    winrt::com_ptr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.put());
    if (FAILED(hr)) {
        return false;
    }
    winrt::com_ptr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(factory.put()));
    if (FAILED(hr)) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // SwapChainPanel composites only premultiplied swap chains. IGNORE is accepted
    // and then never shown, which leaves the XAML background in the window.
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Scaling = DXGI_SCALING_STRETCH;
    hr = factory->CreateSwapChainForComposition(m_device.get(), &desc, nullptr, m_swapChain.put());
    if (FAILED(hr)) {
        return false;
    }

    winrt::com_ptr<ISwapChainPanelNative> native;
    panel.as<IUnknown>()->QueryInterface(IID_PPV_ARGS(native.put()));
    if (!native) {
        return false;
    }
    hr = native->SetSwapChain(m_swapChain.get());
    if (FAILED(hr)) {
        return false;
    }

    D3D11_TEXTURE2D_DESC frame{};
    frame.Width = kWidth;
    frame.Height = kHeight;
    frame.MipLevels = 1;
    frame.ArraySize = 1;
    frame.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    frame.SampleDesc.Count = 1;
    frame.Usage = D3D11_USAGE_DEFAULT;
    hr = m_device->CreateTexture2D(&frame, nullptr, m_frame.put());
    return SUCCEEDED(hr);
}

void StageRenderer::Render() {
    if (!m_context || !m_swapChain || !m_frame) {
        return;
    }
    LARGE_INTEGER qpc{};
    LARGE_INTEGER freq{};
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);
    const int stride = static_cast<int>(kWidth) * 4;
    FillTestPatternBgra(m_bgra.data(), stride, BarXAt(qpc, freq));
    if (m_bitmap) {
        std::memcpy(m_bitmap.PixelBuffer().data(), m_bgra.data(), m_bgra.size());
        m_bitmap.Invalidate();
    }
    m_context->UpdateSubresource(m_frame.get(), 0, nullptr, m_bgra.data(), static_cast<UINT>(stride), 0);

    winrt::com_ptr<ID3D11Texture2D> back;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(back.put())))) {
        return;
    }
    m_context->CopyResource(back.get(), m_frame.get());
    // Flip-model Present fails while any back-buffer reference is outstanding,
    // which leaves the XAML background showing instead of the frame.
    back = nullptr;
    m_context->Flush();
    const HRESULT presented = m_swapChain->Present(1, 0);
    static bool loggedPresent = false;
    if (!loggedPresent) {
        loggedPresent = true;
        wchar_t line[64]{};
        swprintf_s(line, L"Present %08X\r\n", static_cast<unsigned>(presented));
        HANDLE file = CreateFileW(L"C:\\ProgramData\\Callayers\\app.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            char utf8[64]{};
            WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
            WriteFile(file, utf8, static_cast<DWORD>(strlen(utf8)), &written, nullptr);
            CloseHandle(file);
        }
    }

    if (m_frames.OpenOrCreate()) {
        ConvertBgraToNv12(m_bgra.data(), stride, m_nv12.data());
        m_frames.Publish(m_nv12.data());
        m_published = true;
    }
}

} // namespace callayers
