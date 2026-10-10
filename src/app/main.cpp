#include "pch.h"
#include "App.xaml.h"
#include "VirtualCamera.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    if (commandLine && wcsstr(commandLine, L"--register-vcam")) {
        const HRESULT hr = callayers::RegisterInstalledDll();
        return FAILED(hr) ? 1 : 0;
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::Callayers::implementation::App>();
    });
    return 0;
}
