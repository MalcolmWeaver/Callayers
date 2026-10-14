#include "text_editor.h"
#include "text_style.h"
#include "text_clipboard.h"
#include <windowsx.h>
#include <imm.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <winrt/Windows.Data.Json.h>

namespace callayers {
using namespace winrt::Windows::Data::Json;
TextEditor::~TextEditor(){close();}
void TextEditor::releaseBitmap(){
    if(bitmapDC_ && oldBitmap_)SelectObject(bitmapDC_,oldBitmap_);
    if(bitmap_)DeleteObject(bitmap_);if(bitmapDC_)DeleteDC(bitmapDC_);
    bitmapDC_=nullptr;bitmap_=nullptr;oldBitmap_=nullptr;pixels_=nullptr;bitmapWidth_=bitmapHeight_=0;
}
void TextEditor::close(){
    ending_=true;ready_=shown_=false;
    pendingHighSurrogate_=0;
    if(host_)KillTimer(host_,1);
    if(view_){view_->set_editor_listener(nullptr);view_->Unfocus();}
    if(host_){if(GetCapture()==host_)ReleaseCapture();DestroyWindow(host_);host_=nullptr;}
    view_=nullptr;renderer_=nullptr;releaseBitmap();finish_={};
}
void TextEditor::failed(){if(!unavailable_){unavailable_=true;ending_=true;if(finish_)finish_(false);}}
void TextEditor::resize(){
    if(!host_)return;
    int width=std::max(1L,area_.right-area_.left)+2*inset_;
    int height=std::clamp(int(std::ceil(height_*scale_)),1,int(std::max(1L,area_.bottom-area_.top)))+2*inset_;
    RECT current{};GetWindowRect(host_,&current);MapWindowPoints(nullptr,parent_,reinterpret_cast<POINT*>(&current),2);
    RECT desired{area_.left-inset_,area_.top-inset_,area_.left-inset_+width,area_.top-inset_+height};
    if(!EqualRect(&current,&desired))SetWindowPos(host_,HWND_TOP,desired.left,desired.top,width,height,SWP_NOACTIVATE);
    if(view_ && (view_->width()!=unsigned(width) || view_->height()!=unsigned(height)))view_->Resize(width,height);
}
void TextEditor::backdrop(bool clean){
    if(!host_)return;backdropReady_=backdropReady_ || clean;
    if(ready_ && backdropReady_ && !shown_ && !ending_){shown_=true;ShowWindow(host_,SW_SHOWNOACTIVATE);focus();}
    if(shown_)InvalidateRect(host_,nullptr,FALSE);
}
void TextEditor::focus(){
    if(host_ && view_ && ready_ && !unavailable_){
        // A rejected commit (for example text beyond the picture) must remain editable.
        ending_=false;view_->EvaluateScript("window.textEditState.finish='' ");SetFocus(host_);view_->Focus();
    }
}
void TextEditor::open(ultralight::Renderer* renderer,HWND parent,const RECT& area,const std::wstring& text,double font,const std::wstring& color,double width,double scale,double minimumHeight,bool waitForBackdrop,std::function<void(bool)> finish){
    close();parent_=parent;area_=area;text_=text;scale_=scale;height_=minimumHeight;unavailable_=ending_=false;finish_=std::move(finish);
    double dpi=GetDpiForWindow(parent)/96.0;inset_=int(std::ceil(6*dpi));backdropReady_=!waitForBackdrop;
    JsonObject options;options.SetNamedValue(L"text",JsonValue::CreateStringValue(text));options.SetNamedValue(L"color",JsonValue::CreateStringValue(color));
    options.SetNamedValue(L"font",JsonValue::CreateNumberValue(font));options.SetNamedValue(L"width",JsonValue::CreateNumberValue(width));options.SetNamedValue(L"scale",JsonValue::CreateNumberValue(scale));
    options.SetNamedValue(L"dpi",JsonValue::CreateNumberValue(dpi));options.SetNamedValue(L"inset",JsonValue::CreateNumberValue(inset_));options_=options.Stringify();
    try{
        if(!renderer)throw std::runtime_error("Renderer unavailable");renderer_=renderer;
        WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=HostProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CallayersPrivateTextEditor";wc.hCursor=LoadCursorW(nullptr,IDC_IBEAM);RegisterClassW(&wc);
        host_=CreateWindowExW(0,wc.lpszClassName,L"Annotation text",WS_CHILD|WS_CLIPSIBLINGS,0,0,1,1,parent_,nullptr,wc.hInstance,this);
        if(!host_)throw std::runtime_error("Editor window unavailable");resize();RECT bounds{};GetClientRect(host_,&bounds);
        ultralight::ViewConfig config;config.is_accelerated=false;config.is_transparent=true;config.allow_universal_access_from_file_urls=false;config.clipboard_read_policy=ultralight::ClipboardReadPolicy::Deny;
        auto session=renderer_->CreateSession(false,"private-text-editor");
        view_=renderer_->CreateView(bounds.right,bounds.bottom,config,session.get());view_->set_editor_listener(this);
        view_->LoadHTML(winrt::to_string(ReadTextDocument(L"text-editor.html")).c_str());
        opened_=GetTickCount64();if(!SetTimer(host_,1,33,nullptr))throw std::runtime_error("Editor timer unavailable");
    }catch(...){failed();}
}
void TextEditor::poll(){
    if(!ready_ || !view_ || ending_)return;
    ultralight::String error;auto value=view_->EvaluateScript("JSON.stringify(window.textEditState)",&error);
    if(!error.empty())throw std::runtime_error("Editor snapshot failed");
    auto bytes=value.utf8();auto state=JsonObject::Parse(winrt::to_hstring(std::string_view(bytes.data(),bytes.length())));
    auto text=state.GetNamedString(L"text");auto height=state.GetNamedNumber(L"height");
    if(text.size()>2000 || !std::isfinite(height) || height<=0 || height>=2000000)throw std::runtime_error("Invalid editor snapshot");
    text_=text;height_=height;resize();
    auto finish=state.GetNamedString(L"finish",L"");
    if(!finish.empty()){ending_=true;if(finish_)finish_(finish==L"commit");}
}
std::wstring TextEditor::text(){
    // Click-away can finish between timer ticks; collect the current DOM first.
    if(view_ && ready_ && !ending_)try{view_->editor()->FinishComposition();poll();}catch(...){failed();}
    return text_;
}
void TextEditor::tick(){
    if(!view_ || ending_ || ticking_)return;ticking_=true;
    try{
        renderer_->Update();
        if(!ready_){
            if(view_->is_loading()){if(GetTickCount64()-opened_>15000)throw std::runtime_error("Editor loading timed out");ticking_=false;return;}
            ultralight::String error;view_->EvaluateScript(("window.beginTextEdit("+winrt::to_string(options_)+")").c_str(),&error);
            if(!error.empty())throw std::runtime_error("Editor initialization failed");ready_=true;
        }
        poll();if(!ending_){renderer_->Render();backdrop(false);InvalidateRect(host_,nullptr,FALSE);}
    }catch(...){failed();}ticking_=false;
}
void TextEditor::paint(HDC dc){
    RECT bounds{};GetWindowRect(host_,&bounds);MapWindowPoints(nullptr,parent_,reinterpret_cast<POINT*>(&bounds),2);
    int saved=SaveDC(dc);SetViewportOrgEx(dc,-bounds.left,-bounds.top,nullptr);SendMessageW(parent_,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);RestoreDC(dc,saved);
    if(!view_ || !ready_)return;auto surface=view_->surface();if(!surface)return;
    int width=int(surface->width()),height=int(surface->height());
    if(width!=bitmapWidth_ || height!=bitmapHeight_){
        releaseBitmap();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmapDC_=CreateCompatibleDC(dc);bitmap_=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels_,nullptr,0);
        if(!bitmapDC_ || !bitmap_){releaseBitmap();failed();return;}oldBitmap_=SelectObject(bitmapDC_,bitmap_);bitmapWidth_=width;bitmapHeight_=height;
    }
    auto source=static_cast<const uint8_t*>(surface->LockPixels());if(!source)return;
    for(int y=0;y<height;++y)memcpy(static_cast<uint8_t*>(pixels_)+size_t(y)*width*4,source+size_t(y)*surface->row_bytes(),size_t(width)*4);
    surface->UnlockPixels();BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(dc,0,0,width,height,bitmapDC_,0,0,width,height,blend);
}
void TextEditor::key(UINT message,WPARAM wParam,LPARAM lParam){
    if(!ready_ || !view_ || ending_)return;
    bool character=message==WM_CHAR || message==WM_SYSCHAR;bool up=message==WM_KEYUP || message==WM_SYSKEYUP;
    if(character && wParam>=0xD800 && wParam<=0xDBFF){pendingHighSurrogate_=wchar_t(wParam);return;}
    if(character && wParam>=0xDC00 && wParam<=0xDFFF){
        if(pendingHighSurrogate_){std::wstring pair{pendingHighSurrogate_,wchar_t(wParam)};pendingHighSurrogate_=0;view_->editor()->InsertText(winrt::to_string(pair).c_str());poll();renderer_->Render();InvalidateRect(host_,nullptr,FALSE);}return;
    }
    if(character)pendingHighSurrogate_=0;
    if(character && (wParam==VK_ESCAPE || wParam==VK_TAB || (wParam<32 && wParam!=L'\r' && wParam!=L'\n')))return;
    // The Windows constructor handles CR itself; changing it to LF loses Enter.
    if(!character && !up && wParam=='Y' && (GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000)){
        view_->editor()->Execute(ultralight::EditorCommand::Redo);poll();renderer_->Render();InvalidateRect(host_,nullptr,FALSE);return;
    }
    {TextClipboardInput clipboard(host_);
        view_->FireKeyEvent(ultralight::KeyEvent(character?ultralight::KeyEvent::kType_Char:up?ultralight::KeyEvent::kType_KeyUp:ultralight::KeyEvent::kType_RawKeyDown,wParam,lParam,message==WM_SYSKEYDOWN || message==WM_SYSKEYUP || message==WM_SYSCHAR));}
    poll();renderer_->Render();InvalidateRect(host_,nullptr,FALSE);
}
void TextEditor::OnUpdateComposition(ultralight::View*){
    if(!host_ || !view_)return;auto editor=view_->editor();auto rect=editor->HasComposition()?editor->GetCompositionCharacterBounds(0):editor->GetCaretBounds();
    auto context=ImmGetContext(host_);if(!context)return;
    CANDIDATEFORM candidate{};candidate.dwStyle=CFS_EXCLUDE;candidate.ptCurrentPos={LONG(rect.left),LONG(rect.bottom)};candidate.rcArea={LONG(rect.left),LONG(rect.top),LONG(rect.right),LONG(rect.bottom)};ImmSetCandidateWindow(context,&candidate);
    COMPOSITIONFORM form{};form.dwStyle=CFS_POINT;form.ptCurrentPos=candidate.ptCurrentPos;ImmSetCompositionWindow(context,&form);ImmReleaseContext(host_,context);
}
void TextEditor::OnDiscardComposition(ultralight::View*){
    if(discardingComposition_ || !host_)return;discardingComposition_=true;
    auto context=ImmGetContext(host_);if(context){ImmNotifyIME(context,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmReleaseContext(host_,context);}discardingComposition_=false;
}
void TextEditor::composition(LPARAM flags){
    if(!view_ || !ready_ || ending_ || discardingComposition_)return;auto context=ImmGetContext(host_);if(!context)return;
    auto read=[&](DWORD kind){LONG size=ImmGetCompositionStringW(context,kind,nullptr,0);std::wstring text(size>0?size/sizeof(wchar_t):0,L'\0');if(size>0)ImmGetCompositionStringW(context,kind,text.data(),size);return text;};
    auto editor=view_->editor();
    if(flags&GCS_RESULTSTR)editor->CommitComposition(winrt::to_string(read(GCS_RESULTSTR)).c_str());
    if(flags&GCS_COMPSTR){auto text=read(GCS_COMPSTR);auto caret=ImmGetCompositionStringW(context,GCS_CURSORPOS,nullptr,0);uint32_t offset=uint32_t(std::clamp<LONG>(caret,0,LONG(text.size())));editor->SetComposition(winrt::to_string(text).c_str(),nullptr,0,{offset,offset});}
    ImmReleaseContext(host_,context);poll();renderer_->Render();InvalidateRect(host_,nullptr,FALSE);
}
LRESULT CALLBACK TextEditor::HostProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    auto self=reinterpret_cast<TextEditor*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<TextEditor*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wParam,lParam);
    try{switch(message){
    case WM_TIMER:self->tick();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);self->paint(dc);EndPaint(window,&paint);return 0;}
    case WM_GETDLGCODE:return DLGC_WANTALLKEYS|DLGC_WANTCHARS;
    case WM_SETFOCUS:if(self->view_)self->view_->Focus();return 0;
    case WM_KILLFOCUS:
        if(self->view_ && !self->ending_){self->view_->editor()->FinishComposition();self->poll();self->view_->Unfocus();}
        if(self->shown_ && !self->ending_){self->ending_=true;if(self->finish_)self->finish_(true);}return 0;
    case WM_KEYDOWN:case WM_KEYUP:case WM_CHAR:case WM_SYSKEYDOWN:case WM_SYSKEYUP:case WM_SYSCHAR:self->key(message,wParam,lParam);return 0;
    case WM_IME_COMPOSITION:self->composition(lParam);return 0;
    case WM_IME_CHAR:return 0;
    case WM_IME_STARTCOMPOSITION:self->OnUpdateComposition(nullptr);return 0;
    case WM_IME_ENDCOMPOSITION:if(self->view_ && !self->discardingComposition_ && !self->ending_){self->view_->editor()->CancelComposition();self->poll();}return 0;
    case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:case WM_LBUTTONUP:case WM_MOUSEMOVE:
        if(self->ready_ && self->view_ && !self->ending_){
            bool down=message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK;if(down){self->focus();SetCapture(window);}
            self->view_->FireMouseEvent({down?ultralight::MouseEvent::kType_MouseDown:message==WM_LBUTTONUP?ultralight::MouseEvent::kType_MouseUp:ultralight::MouseEvent::kType_MouseMoved,GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam),message==WM_MOUSEMOVE?ultralight::MouseEvent::kButton_None:ultralight::MouseEvent::kButton_Left});
            if(message==WM_LBUTTONUP && GetCapture()==window)ReleaseCapture();self->poll();self->renderer_->Render();InvalidateRect(window,nullptr,FALSE);
        }return 0;
    }}catch(...){self->failed();}
    return DefWindowProcW(window,message,wParam,lParam);
}
}
