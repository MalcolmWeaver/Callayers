#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include "StageRenderer.h"

namespace winrt::Callayers::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

    private:
        callayers::StageRenderer m_renderer;
        bool m_started = false;
    };
}

namespace winrt::Callayers::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
