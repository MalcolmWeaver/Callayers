#include <windows.h>
#include <Ultralight/Ultralight.h>
#include <Ultralight/platform/Clipboard.h>
#include <AppCore/Platform.h>
#include "text_editor.h"
#include "text_clipboard.h"
#include "offline_files.h"
#include <iostream>
#include <thread>
#include <vector>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
class TestClipboard : public ultralight::Clipboard {
public:
    ultralight::String value="synthetic paste";
    unsigned reads=0,writes=0;
    void Clear() override {if(callayers::PrivateTextClipboard().owner)value="";}
    ultralight::RefPtr<ultralight::ClipboardData> Read() override {
        if(!callayers::PrivateTextClipboard().owner)return ultralight::ClipboardData::Create();
        ++reads;return ultralight::ClipboardData::Create(value);
    }
    void Write(ultralight::RefPtr<ultralight::ClipboardData> data) override {
        if(callayers::PrivateTextClipboard().owner){++writes;value=data->AsText();}
    }
};
void key(HWND host,UINT key,bool ctrl=false){
    BYTE prior[256]{},state[256]{};GetKeyboardState(prior);memcpy(state,prior,256);
    if(ctrl)state[VK_CONTROL]=0x80;SetKeyboardState(state);
    SendMessageW(host,WM_KEYDOWN,key,1);SendMessageW(host,WM_KEYUP,key,LPARAM(1)|LPARAM(1ULL<<31));
    SetKeyboardState(prior);
}
}
int main(){try{
    winrt::init_apartment();wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    auto base=std::filesystem::path(exe).parent_path();callayers::OfflineFiles files(base);TestClipboard clipboard;
    auto& platform=ultralight::Platform::instance();ultralight::Config config;config.memory_profile=ultralight::MemoryProfile::LowMemory;config.resource_path_prefix="resources/";platform.set_config(config);
    platform.set_font_loader(ultralight::GetPlatformFontLoader());platform.set_file_system(&files);platform.set_logger(nullptr);platform.set_clipboard(&clipboard);
    auto renderer=ultralight::Renderer::Create();ultralight::ViewConfig options;options.is_accelerated=false;options.is_transparent=true;
    auto output=renderer->CreateView(64,64,options,nullptr);output->LoadHTML("<style>html,body{margin:0;background:#123456}</style>");
    for(int i=0;i<100 && output->is_loading();++i){renderer->Update();std::this_thread::sleep_for(std::chrono::milliseconds(5));}renderer->Render();
    auto snapshot=[&]{auto surface=output->surface();auto pixels=static_cast<const uint8_t*>(surface->LockPixels());require(pixels!=nullptr,"Missing output surface");std::vector<uint8_t> result(pixels,pixels+size_t(surface->row_bytes())*surface->height());surface->UnlockPixels();return result;};
    auto before=snapshot();
    // Hidden synthetic host: tests don't open the app, take focus or alter the OS clipboard.
    auto parent=CreateWindowW(L"STATIC",L"synthetic",WS_OVERLAPPED,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(parent!=nullptr,"Missing synthetic host");
    callayers::TextEditor editor;bool finished=false,committed=false;
    auto open=[&](const std::wstring& text){finished=false;committed=false;editor.open(renderer.get(),parent,{10,10,490,500},text,36,L"#ff0000",480,.75,44,true,[&](bool commit){finished=true;committed=commit;});};
    open(L"start");auto host=FindWindowExW(parent,nullptr,L"CallayersPrivateTextEditor",nullptr);require(host!=nullptr,"Missing private editor");
    for(int i=0;i<100;++i){SendMessageW(host,WM_TIMER,1,0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    require(!editor.unavailable(),"Editor initialization failed");editor.backdrop(true);SendMessageW(host,WM_SETFOCUS,0,0);
    SendMessageW(host,WM_CHAR,L'X',1);require(editor.text()==L"startX","Windows character input lost");
    double oneLine=editor.height();SendMessageW(host,WM_KEYDOWN,VK_RETURN,1);SendMessageW(host,WM_CHAR,L'\r',1);SendMessageW(host,WM_KEYUP,VK_RETURN,1);
    auto newline=editor.text();if(newline!=L"startX\n"){std::cerr<<"Synthetic Enter code units:";for(auto c:newline)std::cerr<<' '<<unsigned(c);std::cerr<<'\n';}
    require(newline==L"startX\n","Enter didn't insert exactly one newline");require(editor.height()>oneLine,"Trailing empty line didn't grow editor");
    SendMessageW(host,WM_CHAR,L'é',1);SendMessageW(host,WM_CHAR,0xD83D,1);SendMessageW(host,WM_CHAR,0xDE00,1);
    auto beforePaste=editor.text();if(beforePaste!=L"startX\né\U0001F600"){std::cerr<<"Synthetic Unicode code units:";for(auto c:beforePaste)std::cerr<<' '<<unsigned(c);std::cerr<<'\n';}require(beforePaste==L"startX\né\U0001F600","Unicode character input failed");
    key(host,'A',true);key(host,'V',true);require(editor.text()==L"synthetic paste","Clipboard paste failed");require(clipboard.reads>0,"Clipboard adapter wasn't used");
    key(host,'A',true);key(host,'C',true);require(clipboard.writes>0,"Clipboard copy failed");
    key(host,'Z',true);require(editor.text()==beforePaste,"Undo failed");key(host,'Y',true);require(editor.text()==L"synthetic paste","Redo failed");
    require(!callayers::PrivateTextClipboard().owner,"Clipboard permission leaked after key event");
    require(callayers::PrivateTextClipboard().Read()->size()==0,"Clipboard exposed outside trusted input");
    require(snapshot()==before,"Private text altered the outgoing renderer surface");
    key(host,VK_ESCAPE);require(finished && !committed,"Escape didn't cancel");editor.close();
    open(L"second");host=FindWindowExW(parent,nullptr,L"CallayersPrivateTextEditor",nullptr);
    for(int i=0;i<100;++i){SendMessageW(host,WM_TIMER,1,0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    editor.backdrop(true);SendMessageW(host,WM_SETFOCUS,0,0);SendMessageW(host,WM_CHAR,L'!',1);SendMessageW(host,WM_KILLFOCUS,0,0);
    if(!(finished && committed && editor.text()==L"second!")){std::cerr<<"Synthetic finish="<<finished<<" commit="<<committed<<" unavailable="<<editor.unavailable()<<" code units:";for(auto c:editor.text())std::cerr<<' '<<unsigned(c);std::cerr<<'\n';}
    require(finished && committed && editor.text()==L"second!","Click-away lost the last input");
    editor.focus();SendMessageW(host,WM_CHAR,L'?',1);require(editor.text()==L"second!?","Rejected commit couldn't return to editing");
    editor.close();DestroyWindow(parent);
    std::cout<<"Ultralight editor input, newline, clipboard, undo, private surface, cancel and click-away passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}catch(...){std::cerr<<"Ultralight editor probe failed\n";return 1;}}
