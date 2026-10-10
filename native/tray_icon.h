#pragma once
#include <shellapi.h>

namespace callayers {
constexpr UINT TrayMessage=WM_APP+40;
constexpr UINT QuitMessage=WM_APP+41;
class TrayIcon {
    NOTIFYICONDATAW data{};
    bool added=false;
public:
    ~TrayIcon(){Remove();}
    bool Add(HWND window){
        if(added)return true;
        data={};data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;
        data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;data.uCallbackMessage=TrayMessage;
        data.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
        wcscpy_s(data.szTip,L"Callayers — open editor or right-click to quit");
        added=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
        if(added){data.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&data);}
        return added;
    }
    void Remove(){if(added)Shell_NotifyIconW(NIM_DELETE,&data);added=false;}
    bool Recreate(HWND window){added=false;return Add(window);}
    static void Open(HWND window){ShowWindow(window,IsIconic(window)?SW_RESTORE:SW_SHOW);SetForegroundWindow(window);}
    void Event(HWND window,LPARAM lp){
        const UINT event=LOWORD(lp);
        if(event==NIN_SELECT||event==NIN_KEYSELECT){Open(window);return;}
        if(event!=WM_CONTEXTMENU)return;
        HMENU menu=CreatePopupMenu();if(!menu)return;
        AppendMenuW(menu,MF_STRING,1,L"Open Callayers");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,2,L"Quit Callayers");
        SetMenuDefaultItem(menu,1,FALSE);
        POINT point{};GetCursorPos(&point);SetForegroundWindow(window);
        UINT choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        if(choice==1)Open(window);
        else if(choice==2)PostMessageW(window,QuitMessage,0,0);
    }
};
}
