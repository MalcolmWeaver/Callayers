#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace callayers {
struct WorkspaceActions {
    std::function<void(int)> camera, selectCard;
    std::function<void(int, bool)> visibility;
    std::function<void(bool)> pen;
    std::function<void()> retry, privacy, hideAll, clear, quit, layout;
};
// All XAML and callbacks stay on the UI thread. Video workers never touch WinUI.
class Workspace {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Workspace(HWND window, WorkspaceActions actions);
    ~Workspace();
    void cameras(const std::vector<std::wstring>& names, int selected);
    void card(int selected, const bool* visible);
    void penMode(bool pen);
    void focus(bool reverse);
    void state(const std::wstring& status, const std::wstring& help, bool fresh, bool privacy);
    void layout(int width, int height, float dpi, const RECT& preview);
    int toolbarHeight() const;
};
class WorkspaceRuntime {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    WorkspaceRuntime();
    ~WorkspaceRuntime();
};
}
