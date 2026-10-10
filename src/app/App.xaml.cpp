#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

void* winrt_make_Callayers_App() {
    return winrt::detach_abi(winrt::make<winrt::Callayers::factory_implementation::App>());
}

void* winrt_make_Callayers_XamlMetaDataProvider() {
    return winrt::detach_abi(winrt::make<winrt::Callayers::factory_implementation::XamlMetaDataProvider>());
}

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Callayers::implementation
{
    App::App()
    {
        InitializeComponent();
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}
