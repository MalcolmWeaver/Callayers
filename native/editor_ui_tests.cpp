#include "text_editor_ui.h"
#include <iostream>
using namespace callayers;
namespace {
TextEditorUI* editor=nullptr;
LRESULT CALLBACK WindowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(editor){LRESULT result;if(editor->Message(msg,wp,lp,result))return result;}
    return DefWindowProcW(window,msg,wp,lp);
}
void Expect(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main(){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    HWND window=nullptr;
    try{
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"CallayersEditorTest";
        RegisterClassW(&wc);window=CreateWindowW(wc.lpszClassName,L"Editor test",WS_OVERLAPPEDWINDOW,0,0,1100,800,nullptr,nullptr,wc.hInstance,nullptr);
        Expect(window!=nullptr,"test window failed");
        std::shared_ptr<const TextOverlay> output;size_t published=0;
        auto controller=std::make_unique<TextEditorUI>(window,[&](auto layer){output=layer;++published;});editor=controller.get();
        editor->SetStage({20,165,980,705});
        SendMessageW(window,WM_COMMAND,TextEditorUI::TextId,0);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(120,245));
        HWND field=GetDlgItem(window,TextEditorUI::EditId);Expect(field!=nullptr,"text tool did not open editor");
        SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Live heading Ω"));Expect(output&&!output->luma.empty(),"typing did not publish text");
        RECT single;GetWindowRect(field,&single);
        SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"First line\r\nSecond line\r\nThird line"));
        RECT multiple;GetWindowRect(field,&multiple);Expect(multiple.bottom-multiple.top>2*(single.bottom-single.top),"typing did not grow the edit box");
        SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Live heading Ω"));
        RECT shrunk;GetWindowRect(field,&shrunk);Expect(shrunk.bottom-shrunk.top==single.bottom-single.top,"typing did not shrink the edit box");
        auto background=Pattern(0,true);auto typing=ComposeText(*background,*output)->pixels;
        Expect(editor->EditingFrame(*background)->pixels==background->pixels,"typing preview contains a duplicate of the edited text");
        // Paint the real edit control over a synthetic camera frame. Its empty area
        // must retain the frame's color, including on subsequent frame refreshes.
        HDC screen=GetDC(window),surface=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,1100,800);
        auto previous=SelectObject(surface,bitmap);RECT canvas{0,0,1100,800};
        SendMessageW(field,EM_SETSEL,0,0);
        for(COLORREF color:{RGB(36,140,180),RGB(180,80,40)}){
            auto brush=CreateSolidBrush(color);FillRect(surface,&canvas,brush);DeleteObject(brush);
            editor->PaintAdorners(surface);
            SendMessageW(field,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface),PRF_CLIENT);
            RECT bounds;GetClientRect(field,&bounds);
            Expect(GetPixel(surface,bounds.right-15,5)==color,"typing background did not preserve the camera frame");
        }
        SelectObject(surface,previous);DeleteObject(bitmap);DeleteDC(surface);ReleaseDC(window,screen);
        SendMessageW(window,WM_APP+20,1,0);
        Expect(GetDlgItem(window,TextEditorUI::EditId)==nullptr,"commit left edit control open");
        Expect(ComposeText(*background,*output)->pixels==typing,"committing text changed outgoing pixels");
        SendMessageW(window,WM_COMMAND,TextEditorUI::UndoId,0);Expect(output->luma.empty(),"creation and typing were not one undo");
        SendMessageW(window,WM_COMMAND,TextEditorUI::RedoId,0);Expect(ComposeText(*background,*output)->pixels==typing,"redo lost text");
        // Selection changes UI state without publishing editing chrome.
        size_t before=published;SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(150,270));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(150,270));
        Expect(published==before,"selection changed outgoing scene");
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(150,270));
        for(int x=160;x<=200;x+=10)SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,290));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(200,290));
        Expect(ComposeText(*background,*output)->pixels!=typing,"drag did not move text");
        SendMessageW(window,WM_COMMAND,TextEditorUI::UndoId,0);Expect(ComposeText(*background,*output)->pixels==typing,"one undo did not reverse the whole drag");
        SendMessageW(window,WM_LBUTTONDBLCLK,0,MAKELPARAM(150,270));field=GetDlgItem(window,TextEditorUI::EditId);Expect(field!=nullptr,"double click did not edit");
        SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Canceled change"));SendMessageW(window,WM_APP+20,0,0);Expect(ComposeText(*background,*output)->pixels==typing,"cancel did not restore original text");
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(150,270));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(150,270));
        SendMessageW(window,WM_COMMAND,TextEditorUI::DeleteId,0);Expect(output->luma.empty(),"delete failed");
        SendMessageW(window,WM_COMMAND,TextEditorUI::UndoId,0);Expect(ComposeText(*background,*output)->pixels==typing,"undo delete failed");
        editor=nullptr;controller.reset();DestroyWindow(window);
        std::cout<<"PASS: text tool, live typing, edit commit/cancel, select without output change, drag coalescing, delete/undo\n";
    }catch(const winrt::hresult_error& error){std::wcerr<<error.message().c_str()<<L"\n";return 1;}
    catch(HRESULT hr){std::wcerr<<ErrorText(hr)<<L"\n";return 1;}
    catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
    return 0;
}
