#include "pch.h"
#include "MainWindow.xaml.h"

#include <winrt/Microsoft.UI.Windowing.h>

#include "MainWindow.g.cpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Callayers::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        AppWindow().Resize({ 980, 600 });
        Closed([this](auto&&, auto&&) { m_renderer.Stop(); });
        auto start = [this] {
            if (m_started) {
                return;
            }
            m_started = true;
            m_renderer.Start(Stage(), Preview(), [this](std::wstring text) {
                StatusText().Text(hstring{ text });
            });
        };
        Stage().Loaded([start](auto&&, auto&&) { start(); });
        if (Stage().IsLoaded()) {
            start();
        }
    }

    MainWindow::~MainWindow()
    {
        m_renderer.Stop();
    }
}

#include "MainWindow.xaml.g.hpp"
