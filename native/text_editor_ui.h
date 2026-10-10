#pragma once
#include "text_renderer.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <functional>
#include <optional>

namespace callayers {
class TextEditorUI {
public:
    enum {SelectId=201,TextId,UndoId,RedoId,DeleteId,FontId,SizeId,BoldId,ColorId,AlignId,NewIdControl,OpenId,SaveId,EditId};
private:
    HWND window,edit=nullptr;
    HFONT editFont=nullptr;
    HDC editBackground=nullptr;
    HBITMAP editBitmap=nullptr;
    HGDIOBJ editOriginal=nullptr;
    SIZE editExtent{};
    std::shared_ptr<const TextOverlay> editingOverlay;
    std::vector<HWND> controls;
    Document document;
    TextRenderer renderer;
    std::function<void(std::shared_ptr<const TextOverlay>)> publish;
    std::filesystem::path path;
    int selected=-1,dragHandle=-1;
    bool textTool=false,dragging=false,syncing=false;
    bool nativeControls=true;
    POINT dragStart{};
    Scene beforeDrag,beforeEdit;
    RECT stage{};
    HWND Control(int id)const{return GetDlgItem(window,id);}
    TextElement* Selected(){return selected>=0&&static_cast<size_t>(selected)<document.scene.texts.size()?&document.scene.texts[selected]:nullptr;}
    HWND Add(const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int width){
        HWND child=CreateWindowW(type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,id>=NewIdControl?20:60,width,28,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
        if(!child)throw HRESULT_FROM_WIN32(GetLastError());
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);controls.push_back(child);return child;
    }
    static std::wstring ReadText(HWND control){int n=GetWindowTextLengthW(control);std::wstring value(static_cast<size_t>(n)+1,L'\0');GetWindowTextW(control,value.data(),n+1);value.resize(n);return value;}
    RECT Rect(const TextElement& t)const{
        int w=stage.right-stage.left,h=stage.bottom-stage.top;
        return {stage.left+static_cast<LONG>(std::lround(t.x*w)),stage.top+static_cast<LONG>(std::lround(t.y*h)),
            stage.left+static_cast<LONG>(std::lround((t.x+t.width)*w)),stage.top+static_cast<LONG>(std::lround((t.y+t.height)*h))};
    }
    std::vector<POINT> Handles(const RECT& r)const{return {{r.left,r.top},{r.right,r.top},{r.right,r.bottom},{r.left,r.bottom},{r.left,(r.top+r.bottom)/2},{r.right,(r.top+r.bottom)/2}};}
    int HitHandle(POINT p){if(!Selected())return -1;auto points=Handles(Rect(*Selected()));for(int i=0;i<6;++i)if(std::abs(p.x-points[i].x)<=7&&std::abs(p.y-points[i].y)<=7)return i;return -1;}
    int HitText(POINT p){for(int i=static_cast<int>(document.scene.texts.size())-1;i>=0;--i){auto r=Rect(document.scene.texts[i]);if(PtInRect(&r,p))return i;}return -1;}
    void Publish(){
        publish(renderer.Render(document.scene));
        if(edit){auto local=document.scene;local.texts.erase(local.texts.begin()+selected);editingOverlay=renderer.Render(local);}
        InvalidateRect(window,nullptr,FALSE);UpdateTitle();
    }
    void ClearEditBackground(){
        if(editBackground&&editOriginal)SelectObject(editBackground,editOriginal);
        if(editBitmap)DeleteObject(editBitmap);if(editBackground)DeleteDC(editBackground);
        editBackground=nullptr;editBitmap=nullptr;editOriginal=nullptr;editExtent={};
    }
    void PaintEditSurface(HWND child,HDC target){
        RECT r;GetClientRect(child,&r);
        HDC buffer=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,std::max(1L,r.right),std::max(1L,r.bottom));
        auto old=SelectObject(buffer,bitmap);
        if(editBackground)BitBlt(buffer,0,0,r.right,r.bottom,editBackground,0,0,SRCCOPY);
        else FillRect(buffer,&r,GetSysColorBrush(COLOR_WINDOW));
        // The native control keeps its selection, clipboard, IME and caret behavior.
        // Paint its text into the same buffer as the current camera background.
        DefSubclassProc(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
        BitBlt(target,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);
        SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);
    }
    void UpdateTitle(){auto name=path.empty()?L"Untitled":path.filename().wstring();std::wstring title=L"Callayers — "+std::wstring(name)+(document.Dirty()?L" *":L"");SetWindowTextW(window,title.c_str());}
    void Sync(){
        if(!nativeControls){UpdateTitle();InvalidateRect(window,nullptr,FALSE);if(StateChanged)StateChanged();return;}
        syncing=true;auto t=Selected();
        for(int id:{DeleteId,FontId,SizeId,BoldId,ColorId,AlignId})EnableWindow(Control(id),t!=nullptr);
        EnableWindow(Control(UndoId),document.CanUndo());EnableWindow(Control(RedoId),document.CanRedo());
        SendMessageW(Control(SelectId),BM_SETCHECK,textTool?BST_UNCHECKED:BST_CHECKED,0);
        SendMessageW(Control(TextId),BM_SETCHECK,textTool?BST_CHECKED:BST_UNCHECKED,0);
        if(t){
            SendMessageW(Control(FontId),CB_SELECTSTRING,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(t->family.c_str()));
            SetWindowTextW(Control(SizeId),std::to_wstring(static_cast<int>(std::lround(t->size))).c_str());
            SendMessageW(Control(BoldId),BM_SETCHECK,t->bold?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(Control(AlignId),CB_SETCURSEL,t->alignment,0);
        }
        syncing=false;UpdateTitle();InvalidateRect(window,nullptr,FALSE);
    }
    void PositionEdit(){
        if(!edit||!Selected())return;auto& t=*Selected();auto r=Rect(t);
        MoveWindow(edit,r.left,r.top,std::max(1L,r.right-r.left),std::max(1L,r.bottom-r.top),FALSE);
        HFONT next=CreateFontW(-std::max(1,static_cast<int>(std::lround(t.size*(stage.bottom-stage.top)/Height))),0,0,0,t.bold?FW_BOLD:FW_NORMAL,
            FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,t.family.c_str());
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);if(editFont)DeleteObject(editFont);editFont=next;
        SendMessageW(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
        RECT content{0,0,std::max(1L,r.right-r.left),std::max(1L,r.bottom-r.top)};SendMessageW(edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));
    }
    static LRESULT CALLBACK EditProc(HWND child,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
        auto self=reinterpret_cast<TextEditorUI*>(ref);
        if(msg==WM_CHAR&&(wp==VK_ESCAPE||(wp==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000))))return 0;
        if(msg==WM_KEYDOWN&&wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(child,EM_SETSEL,0,-1);return 0;}
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(child,&ps);self->PaintEditSurface(child,dc);EndPaint(child,&ps);return 0;}
        if(msg==WM_PRINTCLIENT){self->PaintEditSurface(child,reinterpret_cast<HDC>(wp));return 0;}
        if(msg==WM_KEYDOWN&&(wp==VK_ESCAPE||(wp==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)))){
            PostMessageW(self->window,WM_APP+20,wp==VK_ESCAPE?0:1,0);return 0;
        }
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(child,EditProc,1);
        return DefSubclassProc(child,msg,wp,lp);
    }
    void BeginEdit(const Scene& before){
        if(!Selected())return;beforeEdit=before;auto& t=*Selected();
        DWORD align=t.alignment==1?ES_CENTER:t.alignment==2?ES_RIGHT:ES_LEFT;
        renderer.FitHeight(t);
        edit=CreateWindowW(L"EDIT",t.text.c_str(),WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|align,0,0,1,1,window,reinterpret_cast<HMENU>(EditId),nullptr,nullptr);
        if(!edit)throw HRESULT_FROM_WIN32(GetLastError());
        SendMessageW(edit,EM_SETLIMITTEXT,20000,0);SetWindowSubclass(edit,EditProc,1,reinterpret_cast<DWORD_PTR>(this));
        PositionEdit();SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);Publish();
    }
    void FinishEdit(bool commit){
        if(!edit)return;
        if(commit&&Selected())Selected()->text=ReadText(edit);
        HWND old=edit;edit=nullptr;DestroyWindow(old);ClearEditBackground();editingOverlay.reset();
        if(commit)document.Commit(beforeEdit);else{document.scene=beforeEdit;selected=-1;}
        Publish();Sync();
    }
    std::filesystem::path FileDialog(bool save){
        wchar_t name[32768]{};if(save)wcsncpy_s(name,path.empty()?L"Untitled.callayers.json":path.c_str(),_TRUNCATE);
        OPENFILENAMEW info{sizeof(info)};info.hwndOwner=window;info.lpstrFilter=L"Callayers deck (*.callayers.json)\0*.callayers.json\0JSON files (*.json)\0*.json\0\0";
        info.lpstrFile=name;info.nMaxFile=32768;info.lpstrDefExt=L"callayers.json";
        info.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(save?GetSaveFileNameW(&info):GetOpenFileNameW(&info))return name;
        DWORD error=CommDlgExtendedError();if(error)throw HRESULT_FROM_WIN32(error);return {};
    }
    bool Save(bool saveAs=false){
        FinishEdit(true);auto destination=path;
        if(destination.empty()||saveAs)destination=FileDialog(true);if(destination.empty())return false;
        SaveScene(destination,document.scene);path=destination;document.Saved();Sync();return true;
    }
    void Open(){
        FinishEdit(true);if(!MayDiscard())return;auto candidate=FileDialog(false);if(candidate.empty())return;
        Scene loaded=LoadScene(candidate); // Validate fully before replacing the current scene.
        for(auto& text:loaded.texts)renderer.FitHeight(text);
        auto overlay=renderer.Render(loaded);document.Loaded(std::move(loaded));path=candidate;selected=-1;publish(overlay);Sync();
    }
    void Delete(){FinishEdit(true);if(!Selected())return;auto before=document.scene;document.scene.texts.erase(document.scene.texts.begin()+selected);selected=-1;document.Commit(before);Publish();Sync();}
public:
    std::function<void()> StateChanged;
    std::optional<TextElement> Selection(){if(auto t=Selected())return *t;return {};}
    bool TextTool()const{return textTool;}
    bool CanUndo()const{return document.CanUndo();}
    bool CanRedo()const{return document.CanRedo();}
    bool Dirty()const{return document.Dirty();}
    std::wstring Name()const{return path.empty()?L"Untitled deck":path.filename().wstring();}
    void SaveAs(){Save(true);}
    void Command(int id){LRESULT result;Message(WM_COMMAND,id,0,result);}
    void ChangeStyle(const std::function<void(TextElement&)>& change){
        FinishEdit(true);if(!Selected())return;auto before=document.scene;change(*Selected());
        renderer.FitHeight(*Selected());document.Commit(before);Publish();Sync();
    }
    void ChooseTextColor(){
        ChangeStyle([this](TextElement& t){
            static COLORREF palette[16]{};CHOOSECOLORW color{sizeof(color)};color.hwndOwner=window;color.lpCustColors=palette;color.Flags=CC_FULLOPEN|CC_RGBINIT;
            color.rgbResult=RGB((t.color>>16)&255,(t.color>>8)&255,t.color&255);
            if(ChooseColorW(&color))t.color=(GetRValue(color.rgbResult)<<16)|(GetGValue(color.rgbResult)<<8)|GetBValue(color.rgbResult);
        });
    }
    TextEditorUI(HWND parent,std::function<void(std::shared_ptr<const TextOverlay>)> output,bool legacyControls=true):window(parent),publish(std::move(output)),nativeControls(legacyControls){
        if(!nativeControls){Sync();Publish();return;}
        Add(L"BUTTON",L"Select",BS_AUTOCHECKBOX|BS_PUSHLIKE,SelectId,20,70);Add(L"BUTTON",L"Text",BS_AUTOCHECKBOX|BS_PUSHLIKE,TextId,95,70);
        Add(L"BUTTON",L"Undo",BS_PUSHBUTTON,UndoId,180,65);Add(L"BUTTON",L"Redo",BS_PUSHBUTTON,RedoId,250,65);Add(L"BUTTON",L"Delete",BS_PUSHBUTTON,DeleteId,325,70);
        auto font=Add(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,FontId,410,150);SetWindowPos(font,nullptr,0,0,150,220,SWP_NOMOVE|SWP_NOZORDER);
        for(auto name:{L"Segoe UI",L"Arial",L"Georgia",L"Consolas"})SendMessageW(font,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        auto size=Add(L"EDIT",L"56",WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,SizeId,570,55);SendMessageW(size,EM_SETLIMITTEXT,3,0);
        Add(L"BUTTON",L"Bold",BS_AUTOCHECKBOX|BS_PUSHLIKE,BoldId,635,55);Add(L"BUTTON",L"Color",BS_PUSHBUTTON,ColorId,700,65);
        auto align=Add(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,AlignId,775,110);SetWindowPos(align,nullptr,0,0,110,150,SWP_NOMOVE|SWP_NOZORDER);
        for(auto name:{L"Left",L"Center",L"Right"})SendMessageW(align,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        Add(L"BUTTON",L"New",BS_PUSHBUTTON,NewIdControl,545,65);Add(L"BUTTON",L"Open",BS_PUSHBUTTON,OpenId,615,65);Add(L"BUTTON",L"Save",BS_PUSHBUTTON,SaveId,685,65);
        Sync();Publish();
    }
    ~TextEditorUI(){if(editFont)DeleteObject(editFont);ClearEditBackground();}
    bool IsEditing()const{return edit!=nullptr;}
    std::shared_ptr<Frame> EditingFrame(const Frame& background){return ComposeText(background,*editingOverlay);}
    void RefreshEdit(){if(edit)InvalidateRect(edit,nullptr,FALSE);}
    void SetStage(RECT next){stage=next;PositionEdit();}
    bool MayDiscard(){
        FinishEdit(true);if(!document.Dirty())return true;
        int answer=MessageBoxW(window,L"Save your changes before continuing?",L"Callayers",MB_YESNOCANCEL|MB_ICONQUESTION);
        if(answer==IDCANCEL)return false;if(answer==IDYES)return Save();return true;
    }
    bool Message(UINT msg,WPARAM wp,LPARAM lp,LRESULT& result){
        result=0;
        if(msg==WM_APP+20){FinishEdit(wp!=0);SetFocus(window);return true;}
        if(msg==WM_APP+21){if(edit&&GetFocus()!=edit)FinishEdit(true);return true;}
        if(msg==WM_CTLCOLOREDIT&&reinterpret_cast<HWND>(lp)==edit&&Selected()){
            HDC dc=reinterpret_cast<HDC>(wp);auto color=Selected()->color;SetTextColor(dc,RGB((color>>16)&255,(color>>8)&255,color&255));SetBkMode(dc,TRANSPARENT);result=reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));return true;
        }
        if(msg==WM_COMMAND){
            int id=LOWORD(wp),notification=HIWORD(wp);if(id<SelectId||id>EditId)return false;
            if(syncing)return true;
            if(id==EditId){
                if(notification==EN_CHANGE&&edit&&Selected()){Selected()->text=ReadText(edit);renderer.FitHeight(*Selected());PositionEdit();Publish();}
                if(notification==EN_KILLFOCUS)PostMessageW(window,WM_APP+21,0,0);return true;
            }
            if(id==SizeId&&notification!=EN_KILLFOCUS)return true;
            if((id==FontId||id==AlignId)&&notification!=CBN_SELCHANGE)return true;
            if(id==SelectId||id==TextId){FinishEdit(true);textTool=id==TextId;Sync();SetFocus(window);return true;}
            if(id==SaveId){Save();return true;}if(id==OpenId){Open();return true;}
            if(id==NewIdControl){FinishEdit(true);if(MayDiscard()){document.Loaded({});path.clear();selected=-1;Publish();Sync();}return true;}
            if(id==UndoId||id==RedoId){FinishEdit(true);if(id==UndoId)document.Undo();else document.Redo();selected=-1;Publish();Sync();return true;}
            if(id==DeleteId){Delete();return true;}
            auto requestedFamily=ReadText(Control(FontId));auto requestedSize=ReadText(Control(SizeId));
            bool requestedBold=SendMessageW(Control(BoldId),BM_GETCHECK,0,0)==BST_CHECKED;
            int requestedAlignment=static_cast<int>(SendMessageW(Control(AlignId),CB_GETCURSEL,0,0));
            FinishEdit(true);if(!Selected())return true;auto before=document.scene;auto& t=*Selected();
            if(id==FontId)t.family=requestedFamily;
            if(id==SizeId)t.size=std::clamp(_wtoi(requestedSize.c_str()),8,300);
            if(id==BoldId)t.bold=requestedBold;
            if(id==AlignId)t.alignment=requestedAlignment;
            if(id==ColorId){
                static COLORREF palette[16]{};CHOOSECOLORW color{sizeof(color)};color.hwndOwner=window;color.lpCustColors=palette;color.Flags=CC_FULLOPEN|CC_RGBINIT;
                color.rgbResult=RGB((t.color>>16)&255,(t.color>>8)&255,t.color&255);
                if(ChooseColorW(&color))t.color=(GetRValue(color.rgbResult)<<16)|(GetGValue(color.rgbResult)<<8)|GetBValue(color.rgbResult);
            }
            renderer.FitHeight(t);document.Commit(before);Publish();Sync();return true;
        }
        if(msg==WM_LBUTTONDOWN){
            FinishEdit(true);POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(!PtInRect(&stage,p))return false;SetFocus(window);
            if(textTool){
                if(document.scene.texts.size()>=100)throw std::runtime_error("This scene has reached the 100-text-element limit.");
                Scene before=document.scene;TextElement t;t.id=NewId();t.x=std::clamp(double(p.x-stage.left)/(stage.right-stage.left),0.,.5);t.y=std::clamp(double(p.y-stage.top)/(stage.bottom-stage.top),0.,.8);
                renderer.FitHeight(t);document.scene.texts.push_back(t);selected=static_cast<int>(document.scene.texts.size())-1;textTool=false;Publish();Sync();BeginEdit(before);return true;
            }
            dragHandle=HitHandle(p);if(dragHandle<0)selected=HitText(p);Sync();
            if(Selected()){beforeDrag=document.scene;dragStart=p;dragging=true;SetCapture(window);}return true;
        }
        if(msg==WM_LBUTTONDBLCLK){FinishEdit(true);POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};selected=HitText(p);Sync();if(Selected())BeginEdit(document.scene);return true;}
        if(msg==WM_MOUSEMOVE&&dragging&&Selected()){
            double dx=double(GET_X_LPARAM(lp)-dragStart.x)/std::max(1L,stage.right-stage.left),dy=double(GET_Y_LPARAM(lp)-dragStart.y)/std::max(1L,stage.bottom-stage.top);
            auto& original=beforeDrag.texts[selected];*Selected()=dragHandle<0?MoveText(original,dx,dy):ResizeText(original,dragHandle,dx,dy);if(dragHandle>=0)renderer.FitHeight(*Selected());Publish();return true;
        }
        if((msg==WM_LBUTTONUP||msg==WM_CAPTURECHANGED)&&dragging){dragging=false;document.Commit(beforeDrag);ReleaseCapture();Sync();return true;}
        if(msg==WM_SETCURSOR&&LOWORD(lp)==HTCLIENT){
            POINT p;GetCursorPos(&p);ScreenToClient(window,&p);if(PtInRect(&stage,p)){
                int handle=HitHandle(p);LPCWSTR cursor=handle>=4?IDC_SIZEWE:(handle==0||handle==2)?IDC_SIZENWSE:(handle==1||handle==3)?IDC_SIZENESW:textTool?IDC_IBEAM:IDC_ARROW;
                SetCursor(LoadCursorW(nullptr,cursor));result=TRUE;return true;
            }
        }
        return false;
    }
    bool Key(const MSG& message){
        if(message.message!=WM_KEYDOWN)return false;bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;auto key=message.wParam;
        if(control&&key=='S'){Save((GetKeyState(VK_SHIFT)&0x8000)!=0);return true;}
        if(control&&key=='O'){Open();return true;}
        if(edit)return false;
        if(GetFocus()==Control(SizeId)||GetFocus()==Control(FontId)||GetFocus()==Control(AlignId))return false;
        if(control&&(key=='Z'||key=='Y')){if(key=='Y'||(GetKeyState(VK_SHIFT)&0x8000))document.Redo();else document.Undo();selected=-1;Publish();Sync();return true;}
        if(key==VK_ESCAPE){if(dragging){dragging=false;document.scene=beforeDrag;ReleaseCapture();Publish();}selected=-1;textTool=false;Sync();return true;}
        if(key==VK_DELETE){Delete();return true;}
        if(key==VK_F2&&Selected()){BeginEdit(document.scene);return true;}
        if(!control&&key=='T'){textTool=true;Sync();return true;}
        return false;
    }
    void PaintAdorners(HDC dc){
        if(!Selected())return;auto r=Rect(*Selected());
        if(edit){
            const LONG w=std::max(1L,r.right-r.left),h=std::max(1L,r.bottom-r.top);
            if(!editBackground||editExtent.cx!=w||editExtent.cy!=h){
                ClearEditBackground();editBackground=CreateCompatibleDC(dc);editBitmap=CreateCompatibleBitmap(dc,w,h);
                editOriginal=SelectObject(editBackground,editBitmap);editExtent={w,h};
            }
            BitBlt(editBackground,0,0,w,h,dc,r.left,r.top,SRCCOPY);
            InflateRect(&r,2,2);
        }
        HPEN pen=CreatePen(PS_SOLID,2,RGB(126,119,255));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,GetStockObject(WHITE_BRUSH));
        if(!edit)for(auto p:Handles(r))Rectangle(dc,p.x-4,p.y-4,p.x+5,p.y+5);
        SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);
    }
};
inline void EditorError(HWND owner){
    try{throw;}
    catch(const winrt::hresult_error& e){MessageBoxW(owner,e.message().c_str(),L"Callayers could not complete that action",MB_ICONERROR);}
    catch(HRESULT hr){MessageBoxW(owner,ErrorText(hr).c_str(),L"Callayers could not complete that action",MB_ICONERROR);}
    catch(const std::exception& e){auto text=winrt::to_hstring(e.what());MessageBoxW(owner,text.c_str(),L"Callayers could not complete that action",MB_ICONERROR);}
    catch(...){MessageBoxW(owner,L"The action could not be completed.",L"Callayers",MB_ICONERROR);}
}
}
