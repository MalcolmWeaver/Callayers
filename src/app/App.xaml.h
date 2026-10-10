#pragma once

#include "App.g.h"
#include "App.xaml.g.h"

namespace winrt::Callayers::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        winrt::Callayers::MainWindow window{ nullptr };
    };
}

namespace winrt::Callayers::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
