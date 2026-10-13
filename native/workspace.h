#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "annotations.h"

namespace callayers {
struct WorkspaceActions {
    std::function<void(int)> camera, selectCard;
    std::function<void(int, bool)> visibility;
    std::function<void(AnnotationTool)> tool;
    std::function<void(std::wstring)> color;
    std::function<void(int)> size;
    std::function<bool(std::wstring,bool,double)> text;
    std::function<void()> retry, privacy, hideAll, clear, quit, layout;
};
// All XAML and callbacks stay on the UI thread. Video workers never touch WinUI.
class Workspace {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Workspace(HWND window, WorkspaceActions actions);
    ~Workspace();
    void cameras(const std::vector<std::wstring>& names, int selected, const std::wstring& missingName=L"");
    void card(int selected, const bool* visible);
    void annotationControls(AnnotationTool tool,const std::wstring& color,int size);
    void editText(const RECT& area,const std::wstring& value,double font,const std::wstring& color,double width,double scale,double minimumHeight,bool waitForBackdrop);
    void textBackdrop(bool clean);
    bool finishText(bool commit=true);
    bool editingText() const;
    void focus(bool reverse);
    void state(const std::wstring& status, const std::wstring& help, bool fresh, bool privacy, bool retryPreference=false);
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
