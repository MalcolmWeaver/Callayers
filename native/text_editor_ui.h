#pragma once
#include "text_renderer.h"
#include "sample_cards.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <functional>
#include <optional>
#include <shellapi.h>

namespace callayers {
class TextEditorUI {
public:
    enum {SelectId=201,TextId,UndoId,RedoId,DeleteId,FontId,SizeId,BoldId,ColorId,AlignId,NewIdControl,OpenId,SaveId,EditId};
    enum class DrawTool {None,Pen,Eraser,Laser,Arrow,Rectangle,Ellipse};
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
    int selected=-1,selectedCard=-1,selectedImage=-1,selectedInk=-1,dragHandle=-1;
    std::vector<CardElement> playing;
    std::wstring focusedCard;
    CardElement* FocusedCard(){for(auto& card:document.scene.cards)if(card.id==focusedCard)return &card;return nullptr;}
    double textSize=56;
    bool annotationMode=false,interacting=false;
    bool cardPointerCaptured=false,cardPointerHover=false;
    wchar_t pendingSurrogate=0;
    POINT interactionPoint{};
    bool ShapeTool()const{return drawTool==DrawTool::Arrow||drawTool==DrawTool::Rectangle||drawTool==DrawTool::Ellipse;}
    InkStroke* SelectedInk(){return selectedInk>=0&&size_t(selectedInk)<document.scene.ink.size()?&document.scene.ink[selectedInk]:nullptr;}
    struct InkBounds {double x,y,width,height;};
    InkBounds Bounds(const InkStroke& stroke)const{
        double x=1,y=1,r=0,b=0;for(auto p:stroke.points){x=std::min(x,p.x);y=std::min(y,p.y);r=std::max(r,p.x);b=std::max(b,p.y);}
        return {x,y,std::max(.005,r-x),std::max(.005,b-y)};
    }
    bool CardOnStage(){return SelectedCard()&&IsCardShown(SelectedCard()->id);}
    void PublishCards(){
        Scene output;
        std::erase_if(playing,[&](const auto& live){return std::none_of(document.scene.cards.begin(),document.scene.cards.end(),[&](const auto& card){return card.id==live.id;});});
        for(const auto& card:document.scene.cards)for(auto& live:playing)if(card.id==live.id){live=card;output.cards.push_back(card);}
        if(SceneChanged)SceneChanged(output);
    }
    bool textTool=false,dragging=false,syncing=false;
    bool nativeControls=true;
    DrawTool drawTool=DrawTool::None;
    bool marking=false,inkPending=false;
    uint32_t penColor=0xff0000,shapeColor=0xffffff,textColor=0xffffff;
    double penWidth=6;
    size_t inkPointCount=0;
    Scene beforeInk;
    InkPoint lastInk{},shapeOrigin{};
    struct LaserSample {InkPoint point;Clock::time_point time;bool start=false;};
    std::vector<LaserSample> laser;
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
    template<class Element> RECT Rect(const Element& t)const{
        int w=stage.right-stage.left,h=stage.bottom-stage.top;
        return {stage.left+static_cast<LONG>(std::lround(t.x*w)),stage.top+static_cast<LONG>(std::lround(t.y*h)),
            stage.left+static_cast<LONG>(std::lround((t.x+t.width)*w)),stage.top+static_cast<LONG>(std::lround((t.y+t.height)*h))};
    }
    std::vector<POINT> Handles(const RECT& r)const{return {{r.left,r.top},{r.right,r.top},{r.right,r.bottom},{r.left,r.bottom},{r.left,(r.top+r.bottom)/2},{r.right,(r.top+r.bottom)/2}};}
    CardElement* SelectedCard(){return selectedCard>=0&&static_cast<size_t>(selectedCard)<document.scene.cards.size()?&document.scene.cards[selectedCard]:nullptr;}
    ImageElement* SelectedImage(){return selectedImage>=0&&static_cast<size_t>(selectedImage)<document.scene.images.size()?&document.scene.images[selectedImage]:nullptr;}
    bool HasSelection(){return Selected()||SelectedImage()||SelectedInk()||CardOnStage();}
    RECT SelectionRect(){return Selected()?Rect(*Selected()):SelectedImage()?Rect(*SelectedImage()):SelectedInk()?Rect(Bounds(*SelectedInk())):Rect(*SelectedCard());}
    RECT CardMoveRect(){
        auto r=Rect(*SelectedCard());double dpi=GetDpiForWindow(window)/96.;LONG w=LONG(80*dpi),h=LONG(20*dpi),gap=LONG(4*dpi);
        LONG x=(r.left+r.right-w)/2;return {x,r.top-gap-h,x+w,r.top-gap};
    }
    bool HitCardMove(POINT p){if(!CardOnStage())return false;auto r=CardMoveRect();return PtInRect(&r,p)!=FALSE;}
    bool AnnotationHit(POINT p){
        if(!annotationMode)return false;if(HitText(p)>=0)return true;
        auto point=StagePoint(p);for(const auto& stroke:document.scene.ink)if(InkHit(stroke,point,point,10))return true;return false;
    }
    int HitCard(POINT p){for(int i=int(document.scene.cards.size())-1;i>=0;--i){const auto& card=document.scene.cards[i];if(IsCardShown(card.id)){auto r=Rect(card);if(PtInRect(&r,p))return i;}}return -1;}
    int HitHandle(POINT p){if(!HasSelection())return -1;auto points=Handles(SelectionRect());for(int i=0;i<6;++i)if(std::abs(p.x-points[i].x)<=7&&std::abs(p.y-points[i].y)<=7)return i;return -1;}
    int HitText(POINT p){int hit=-1;for(int i=0;i<static_cast<int>(document.scene.texts.size());++i){auto r=Rect(document.scene.texts[i]);if(PtInRect(&r,p)&&(hit<0||document.scene.texts[i].order>=document.scene.texts[hit].order))hit=i;}return hit;}
    void HitSelection(POINT p){
        selectedInk=-1;
        if(annotationMode){auto point=StagePoint(p);for(int i=int(document.scene.ink.size())-1;i>=0;--i)if(InkHit(document.scene.ink[i],point,point,10)){selectedInk=i;selected=selectedImage=selectedCard=-1;return;}}
        selected=HitText(p);selectedImage=-1;
        for(int i=0;i<static_cast<int>(document.scene.images.size());++i){auto& image=document.scene.images[i];auto r=Rect(image);if(PtInRect(&r,p)&&(selectedImage<0||image.order>=document.scene.images[selectedImage].order))selectedImage=i;}
        if(selected>=0&&selectedImage>=0){if(document.scene.texts[selected].order>=document.scene.images[selectedImage].order)selectedImage=-1;else selected=-1;}
        selectedCard=selected<0&&selectedImage<0?HitCard(p):-1;if(SelectedCard())focusedCard=SelectedCard()->id;
    }
    std::vector<InkMark> LaserMarks(Clock::time_point now){
        std::vector<InkMark> marks;
        for(size_t i=0;i<laser.size();++i){float alpha=static_cast<float>(std::clamp(1.-std::chrono::duration<double>(now-laser[i].time).count()/.9,0.,1.));if(alpha<=0)continue;
            InkStroke stroke;stroke.color=0xff647c;stroke.width=10;stroke.points={laser[i].point};if(i&&!laser[i].start)stroke.points.insert(stroke.points.begin(),laser[i-1].point);
            auto glow=stroke;glow.width=22;marks.push_back({std::move(glow),alpha*.18f});marks.push_back({std::move(stroke),alpha});
        }return marks;
    }
    void Publish(bool notify=true,Clock::time_point now=Clock::now()){
        auto marks=LaserMarks(now);publish(renderer.Render(document.scene,marks));if(notify)PublishCards();
        if(edit){auto local=document.scene;local.texts.erase(local.texts.begin()+selected);editingOverlay=renderer.Render(local,marks);}
        InvalidateRect(window,nullptr,FALSE);UpdateTitle();
    }
    InkPoint StagePoint(POINT p)const{return {std::clamp(double(p.x-stage.left)/std::max(1L,stage.right-stage.left),0.,1.),std::clamp(double(p.y-stage.top)/std::max(1L,stage.bottom-stage.top),0.,1.)};}
    void InkMove(POINT p,bool first=false){
        auto point=StagePoint(p);
        if(ShapeTool()){
            auto& stroke=document.scene.ink.back();auto a=shapeOrigin;stroke.points={a};
            auto add=[&](double x,double y){stroke.points.push_back({std::clamp(x,0.,1.),std::clamp(y,0.,1.)});};
            if(drawTool==DrawTool::Rectangle){add(point.x,a.y);add(point.x,point.y);add(a.x,point.y);add(a.x,a.y);}
            else if(drawTool==DrawTool::Ellipse){
                double cx=(a.x+point.x)/2,cy=(a.y+point.y)/2,rx=(point.x-a.x)/2,ry=(point.y-a.y)/2;
                // Keep the drag origin separately: an ellipse's first visible point is on its perimeter.
                stroke.points.clear();for(int i=0;i<=64;++i){double angle=6.283185307179586*i/64;add(cx+rx*std::cos(angle),cy+ry*std::sin(angle));}
            }else{
                double dx=(point.x-a.x)*Width,dy=(point.y-a.y)*Height,length=std::hypot(dx,dy);
                double ux=length>0?dx/length:1,uy=length>0?dy/length:0;
                double head=std::min(std::max(24.,penWidth*4),length*.45),half=std::min(penWidth/2,length*.1);
                stroke.points.clear();auto arrowPoint=[&](double along,double across){add(a.x+(ux*along-uy*across)/Width,a.y+(uy*along+ux*across)/Height);};
                arrowPoint(0,half);arrowPoint(length-head,half);arrowPoint(length-head,head*.5);arrowPoint(length,0);arrowPoint(length-head,-head*.5);arrowPoint(length-head,-half);arrowPoint(0,-half);arrowPoint(0,half);
            }
        }else if(drawTool==DrawTool::Pen){
            auto& stroke=document.scene.ink.back();
            if(!first&&stroke.points.size()<MaxStrokePoints&&inkPointCount<MaxInkPoints&&PointSegmentDistance(point,stroke.points.back(),stroke.points.back())>=2){stroke.points.push_back(point);++inkPointCount;}
        }else if(drawTool==DrawTool::Eraser){
            std::erase_if(document.scene.ink,[&](const auto& stroke){return InkHit(stroke,lastInk,point,18);});
        }else if(drawTool==DrawTool::Laser){
            if(first||laser.empty()||PointSegmentDistance(point,laser.back().point,laser.back().point)>=2){laser.push_back({point,Clock::now(),first});if(laser.size()>256)laser.erase(laser.begin());}
        }
        lastInk=point;inkPending=true;
    }
    void FinishInk(bool commit=true){
        if(!marking)return;marking=false;ReleaseCapture();
        if(drawTool!=DrawTool::Laser){if(commit)document.Commit(beforeInk);else document.scene=beforeInk;}
        else if(!commit)laser.clear();
        inkPending=false;Publish(false);Sync();
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
        document.BeginTextAnnotationSession(beforeEdit);
        SendMessageW(edit,EM_SETLIMITTEXT,20000,0);SetWindowSubclass(edit,EditProc,1,reinterpret_cast<DWORD_PTR>(this));
        PositionEdit();SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);Publish();
    }
    void FinishEdit(bool commit){
        if(!edit)return;
        if(commit&&Selected())Selected()->text=ReadText(edit);
        HWND old=edit;edit=nullptr;DestroyWindow(old);ClearEditBackground();editingOverlay.reset();
        if(commit&&Selected()&&Selected()->text.find_first_not_of(L" \t\r\n")==std::wstring::npos){
            const auto id=Selected()->id;
            if(std::none_of(beforeEdit.texts.begin(),beforeEdit.texts.end(),[&](const auto& text){return text.id==id;})){
                document.scene.texts.erase(document.scene.texts.begin()+selected);selected=-1;
            }
        }
        if(commit)document.Commit(beforeEdit);else{document.scene=beforeEdit;selected=selectedCard=selectedImage=selectedInk=-1;}
        document.EndTextAnnotationSession(commit);
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
        if(FlushCardState)FlushCardState();
        FinishInk();FinishEdit(true);auto destination=path;
        if(destination.empty()||saveAs)destination=FileDialog(true);if(destination.empty())return false;
        auto saved=document.scene;saved.texts.clear();saved.ink.clear();SaveScene(destination,saved);path=destination;document.Saved();Sync();return true;
    }
    static std::wstring HtmlEscape(const std::wstring& value){std::wstring result;for(auto c:value){switch(c){case L'&':result+=L"&amp;";break;case L'<':result+=L"&lt;";break;case L'>':result+=L"&gt;";break;case L'\"':result+=L"&quot;";break;default:result+=c;}}return result;}
    static CardElement LegacyAnnotations(const Scene& scene){
        CardElement card;card.id=NewId();card.name=L"Imported annotations";card.x=card.y=0;card.width=card.height=1;
        card.html=L"<!doctype html><html><head><style>html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent}.text{position:absolute;white-space:pre-wrap;overflow-wrap:break-word;line-height:normal}</style></head><body>";
        auto hex=[](uint32_t color){wchar_t buffer[8];swprintf_s(buffer,L"#%06x",color);return std::wstring(buffer);};
        for(const auto& text:scene.texts){card.html+=L"<div class=\"text\" style=\"left:"+std::to_wstring(text.x*100)+L"%;top:"+std::to_wstring(text.y*100)+L"%;width:"+std::to_wstring(text.width*100)+L"%;font-size:"+std::to_wstring(text.size/19.2)+L"vw;color:"+hex(text.color)+L";font-family:"+HtmlEscape(text.family)+L";font-weight:"+(text.bold?L"bold":L"normal")+L";text-align:"+(text.alignment==1?L"center":text.alignment==2?L"right":L"left")+L"\">"+HtmlEscape(text.text)+L"</div>";}
        card.html+=L"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1920 1080\" preserveAspectRatio=\"none\" style=\"position:absolute;inset:0;width:100%;height:100%\">";
        for(const auto& stroke:scene.ink){if(stroke.points.empty())continue;auto points=stroke.points;if(points.size()==1)points.push_back(points.front());card.html+=L"<polyline fill=\""+(stroke.filled?hex(stroke.color):std::wstring(L"none"))+L"\" stroke=\""+hex(stroke.color)+L"\" stroke-width=\""+std::to_wstring(stroke.width)+L"\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";for(auto point:points)card.html+=std::to_wstring(point.x*Width)+L","+std::to_wstring(point.y*Height)+L" ";card.html+=L"\"/>";}
        card.html+=L"</svg></body></html>";if(card.html.size()>200000)throw std::runtime_error("This older deck has too many annotations to convert into one card. Simplify it in the previous version first. The original file has not changed.");return card;
    }
    void Open(){
        FinishInk();FinishEdit(true);if(!MayDiscard())return;auto candidate=FileDialog(false);if(candidate.empty())return;
        Scene loaded=LoadScene(candidate); // Validate fully before replacing the current scene.
        if(loaded.cards.size()+loaded.images.size()+(!loaded.texts.empty()||!loaded.ink.empty()?1:0)>12)throw std::runtime_error("This older deck has more than 12 cards/images. Split it into smaller decks before opening here.");
        for(const auto& image:loaded.images){auto card=ImageCard(image);card.width=std::max(.08,image.width);card.height=std::max(.08,image.height);card.x=std::min(image.x,1.-card.width);card.y=std::min(image.y,1.-card.height);loaded.cards.push_back(std::move(card));}loaded.images.clear();
        if(!loaded.texts.empty()||!loaded.ink.empty()){loaded.cards.push_back(LegacyAnnotations(loaded));loaded.texts.clear();loaded.ink.clear();}
        for(auto& text:loaded.texts)renderer.FitHeight(text);
        auto overlay=renderer.Render(loaded);interacting=false;laser.clear();playing.clear();focusedCard.clear();selectedInk=-1;document.Loaded(std::move(loaded));path=candidate;selected=selectedCard=selectedImage=selectedInk=-1;publish(overlay);PublishCards();Sync();
    }
    void Delete(){if(FlushCardState)FlushCardState();FinishEdit(true);if(!HasSelection()&&!SelectedCard())return;auto before=document.scene;
        if(SelectedInk())document.scene.ink.erase(document.scene.ink.begin()+selectedInk);else if(Selected())document.scene.texts.erase(document.scene.texts.begin()+selected);else if(SelectedImage())document.scene.images.erase(document.scene.images.begin()+selectedImage);else document.scene.cards.erase(document.scene.cards.begin()+selectedCard);
        selected=selectedCard=selectedImage=selectedInk=-1;document.Commit(before);Publish();Sync();}

public:
    bool ImageSelected(){return SelectedImage()!=nullptr;}
    static CardElement ImageCard(const ImageElement& image){
        if(!image.asset)throw std::runtime_error("Image data is missing.");
        CardElement card;card.id=image.id.empty()?NewId():image.id;card.name=image.name.empty()?L"Image":image.name.substr(0,100);
        auto source=image.asset;
        // Retain an embedded raster asset, reducing large files to the card asset budget.
        if(source->png.size()+22>MaxCardAssetChars){
            DWORD count=0;Check(CryptStringToBinaryW(source->png.c_str(),static_cast<DWORD>(source->png.size()),CRYPT_STRING_BASE64,nullptr,&count,nullptr,nullptr)?S_OK:E_FAIL);
            std::vector<BYTE> bytes(count);Check(CryptStringToBinaryW(source->png.c_str(),static_cast<DWORD>(source->png.size()),CRYPT_STRING_BASE64,bytes.data(),&count,nullptr,nullptr)?S_OK:E_FAIL);
            ImageCodec codec;for(UINT dimension=1024;dimension>=128;dimension/=2){source=codec.Import(bytes,dimension);if(source->png.size()+22<=MaxCardAssetChars)break;}
        }
        CardAsset asset;asset.id=L"imported_image";asset.description=image.name.substr(0,240);asset.width=source->width;asset.height=source->height;
        asset.dataUri=std::make_shared<const std::wstring>(L"data:image/png;base64,"+source->png);
        card.assets={asset};ValidateCardAssets(card.assets);
        card.html=LR"(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><style>html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent}img{width:100%;height:100%;object-fit:contain;display:block}</style></head><body><img src="callayers-asset:imported_image" alt="Imported image"></body></html>)";
        double fit=std::min(Width*.42/image.asset->width,Height*.5/image.asset->height);card.width=std::max(.08,image.asset->width*fit/Width);card.height=std::max(.08,image.asset->height*fit/Height);return card;
    }
    void AddImages(std::vector<ImageElement> images,std::optional<POINT> drop={}){
        if(images.empty())return;
        if(document.scene.cards.size()+images.size()>12||ElementCount(document.scene)+images.size()>100)throw std::runtime_error("A deck can hold up to 12 cards. Remove a card first.");
        FinishInk();FinishEdit(true);auto before=document.scene,next=before;
        size_t chars=0;for(const auto& c:next.cards)for(const auto& a:c.assets)chars+=a.dataUri->size();
        for(const auto& image:images){auto card=ImageCard(image);chars+=card.assets.front().dataUri->size();
            if(drop){auto point=StagePoint(*drop);card.x=std::clamp(point.x-card.width/2,0.,1.-card.width);card.y=std::clamp(point.y-card.height/2,0.,1.-card.height);}next.cards.push_back(std::move(card));}
        if(chars>MaxSceneImageChars)throw std::runtime_error("This deck has reached its embedded image limit.");
        document.scene=std::move(next);document.Commit(before);selected=selectedImage=selectedInk=-1;selectedCard=int(document.scene.cards.size())-1;focusedCard=document.scene.cards.back().id;textTool=false;drawTool=DrawTool::None;
        Publish();Sync();SetFocus(window);
    }
    void ImportImages(const std::vector<std::filesystem::path>& paths,std::optional<POINT> drop={}){
        if(paths.size()>16)throw std::runtime_error("Import up to 16 images at a time.");ImageCodec codec;std::vector<ImageElement> images;
        for(const auto& file:paths){ImageElement image;image.name=file.filename().wstring();if(image.name.size()>260)image.name.resize(260);try{image.asset=codec.LoadFile(file);}catch(HRESULT){throw std::runtime_error("Could not read an image. Try PNG, JPEG, BMP, or GIF.");}catch(const winrt::hresult_error&){throw std::runtime_error("Could not read an image. Try PNG, JPEG, BMP, or GIF.");}images.push_back(std::move(image));}AddImages(std::move(images),drop);
    }
    void ChooseImage(){
        wchar_t name[32768]{};OPENFILENAMEW info{sizeof(info)};info.hwndOwner=window;info.lpstrFile=name;info.nMaxFile=32768;
        info.lpstrTitle=L"Add an image";info.lpstrFilter=L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0All files\0*.*\0\0";info.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(GetOpenFileNameW(&info))ImportImages({std::filesystem::path(name)});
    }
    static std::vector<std::filesystem::path> DropPaths(HDROP drop){
        UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);if(count>16)throw std::runtime_error("Import up to 16 images at a time.");std::vector<std::filesystem::path> paths;
        for(UINT i=0;i<count;++i){UINT length=DragQueryFileW(drop,i,nullptr,0);std::wstring path(length+1,L'\0');DragQueryFileW(drop,i,path.data(),length+1);path.resize(length);paths.emplace_back(path);}return paths;
    }
    bool PasteImage(){
        std::vector<BYTE> bytes;std::vector<std::filesystem::path> files;bool dib=false;
        {if(!OpenClipboard(window))throw std::runtime_error("The clipboard is busy. Try pasting again.");struct Close {~Close(){CloseClipboard();}} close;
            if(IsClipboardFormatAvailable(CF_HDROP))files=DropPaths(static_cast<HDROP>(GetClipboardData(CF_HDROP)));
            else{UINT png=RegisterClipboardFormatW(L"PNG");UINT format=IsClipboardFormatAvailable(png)?png:IsClipboardFormatAvailable(CF_DIBV5)?CF_DIBV5:IsClipboardFormatAvailable(CF_DIB)?CF_DIB:0;if(!format)return false;dib=format!=png;
                HGLOBAL memory=GetClipboardData(format);size_t size=GlobalSize(memory);if(!size||size>MaxImageBytes)throw std::runtime_error("Clipboard image is too large.");auto data=static_cast<const BYTE*>(GlobalLock(memory));if(!data)throw std::runtime_error("Could not read the clipboard image.");
                try{bytes.assign(data,data+size);}catch(...){GlobalUnlock(memory);throw;}GlobalUnlock(memory);}
        }
        if(!files.empty()){ImportImages(files);return true;}
        ImageCodec codec;ImageElement image;image.name=L"Pasted image";image.asset=dib?codec.FromDib(bytes.data(),bytes.size()):codec.Import(bytes);AddImages({std::move(image)});return true;
    }
    void OrderForeground(bool front){
        FinishInk();FinishEdit(true);if(!Selected()&&!SelectedImage())return;auto before=document.scene;int* selectedOrder=Selected()?&Selected()->order:&SelectedImage()->order;std::vector<int*> orders;
        for(auto& image:document.scene.images)orders.push_back(&image.order);for(auto& text:document.scene.texts)orders.push_back(&text.order);
        std::stable_sort(orders.begin(),orders.end(),[](auto a,auto b){return *a<*b;});std::erase(orders,selectedOrder);orders.insert(front?orders.end():orders.begin(),selectedOrder);for(size_t i=0;i<orders.size();++i)*orders[i]=static_cast<int>(i);document.Commit(before);Publish();Sync();
    }
    DrawTool DrawingTool()const{return drawTool;}
    uint32_t PenColor()const{return penColor;}
    double PenWidth()const{return penWidth;}
    void SetPenColor(uint32_t color){FinishInk();penColor=color&0xffffff;Sync();}
    void SetPenWidth(double width){FinishInk();if(std::isfinite(width))penWidth=std::clamp(width,1.,40.);Sync();}
    void SetDrawingTool(DrawTool tool){interacting=false;FinishInk();FinishEdit(true);drawTool=tool;textTool=false;selected=selectedCard=selectedImage=selectedInk=-1;Sync();SetFocus(window);}
    void TickInk(Clock::time_point now=Clock::now()){
        bool hadLaser=!laser.empty();std::erase_if(laser,[&](const auto& sample){return now-sample.time>=std::chrono::milliseconds(900);});
        if(inkPending||hadLaser){inkPending=false;Publish(false,now);}
    }
    std::function<void()> StateChanged;
    void SetAnnotationObserver(std::function<void(const AnnotationChange&)> observer){document.AnnotationsChanged=std::move(observer);}
    std::function<void(const Scene&)> SceneChanged;
    std::function<void(UINT,POINT,WPARAM)> CardMouse;
    std::function<void(UINT,WPARAM,LPARAM)> CardKey;
    std::function<void(const std::wstring&)> CardText;
    std::function<void()> FlushCardState;
    uint64_t DocumentEpoch()const{return document.Epoch();}
    bool Interacting()const{return interacting;}
    void SetInteract(bool value){FinishInk();FinishEdit(true);if(cardPointerCaptured){cardPointerCaptured=false;if(CardMouse)CardMouse(WM_LBUTTONUP,interactionPoint,0);ReleaseCapture();}interacting=value;pendingSurrogate=0;drawTool=DrawTool::None;textTool=false;selected=selectedImage=selectedInk=selectedCard=-1;Sync();SetFocus(window);}
    void ApplyCardState(const std::wstring& id,uint64_t epoch,const std::wstring& html,const std::wstring& state){
        if(epoch!=document.Epoch()||state.size()>65536)return;
        for(auto& card:document.scene.cards)if(card.id==id&&card.html==html){if(card.state==state)return;auto before=document.scene;card.state=state;document.Commit(before);PublishCards();UpdateTitle();return;}
    }
    bool CardSelected(){return FocusedCard()!=nullptr;}
    const Scene& CurrentScene()const{return document.scene;}
    int SelectedCardIndex()const{for(int i=0;i<int(document.scene.cards.size());++i)if(document.scene.cards[i].id==focusedCard)return i;return -1;}
    bool IsCardShown(const std::wstring& id)const{return std::any_of(playing.begin(),playing.end(),[&](const auto& card){return card.id==id;});}
    bool HasShownCards()const{return !playing.empty();}
    void RenameCard(const std::wstring& id,std::wstring name){
        auto first=name.find_first_not_of(L" \t\r\n");if(first==std::wstring::npos)return;name=name.substr(first,name.find_last_not_of(L" \t\r\n")-first+1);name.resize(std::min(size_t(100),name.size()));
        FinishInk();FinishEdit(true);auto before=document.scene;for(auto& card:document.scene.cards)if(card.id==id)card.name=name;document.Commit(before);Sync();
    }
    void SetCardShown(int index,bool show){
        if(FlushCardState)FlushCardState();FinishInk();FinishEdit(true);if(index<0||size_t(index)>=document.scene.cards.size())return;auto card=document.scene.cards[index];
        document.ClearAnnotations();laser.clear();std::erase_if(playing,[&](const auto& live){return live.id==card.id;});if(show)playing.push_back(card);
        textTool=false;drawTool=DrawTool::None;selected=selectedImage=selectedInk=-1;selectedCard=index;Publish();Sync();
    }
    void SelectCard(int index){interacting=false;FinishInk();FinishEdit(true);selected=selectedImage=selectedInk=-1;selectedCard=index;focusedCard=SelectedCard()?SelectedCard()->id:L"";textTool=false;drawTool=DrawTool::None;Sync();}
    void HideCard(){if(FlushCardState)FlushCardState();FinishInk();FinishEdit(true);document.ClearAnnotations();laser.clear();playing.clear();textTool=false;drawTool=DrawTool::None;selected=selectedImage=selectedInk=selectedCard=-1;Publish();Sync();}
    void ClearAnnotations(){FinishInk();FinishEdit(true);auto before=document.scene;document.scene.texts.clear();document.scene.ink.clear();laser.clear();selected=selectedImage=selectedInk=-1;document.Commit(before,true);Publish();Sync();}
    void EnableAnnotationSelection(){annotationMode=true;}
    uint32_t AnnotationColor(){return Selected()?Selected()->color:SelectedInk()?SelectedInk()->color:ShapeTool()?shapeColor:textTool?textColor:penColor;}
    int AnnotationSize(){double value=Selected()?Selected()->size:textTool?textSize:SelectedInk()?SelectedInk()->width:penWidth;return Selected()||textTool?(value<=32?0:value<=56?1:2):(value<=3?0:value<=6?1:2);}
    void MoveSelectedCard(int direction){FinishInk();FinishEdit(true);int next=selectedCard+direction;if(!SelectedCard()||next<0||size_t(next)>=document.scene.cards.size())return;auto before=document.scene;std::swap(document.scene.cards[selectedCard],document.scene.cards[next]);selectedCard=next;document.Commit(before);PublishCards();Sync();}
    void SetAnnotationMode(bool value){annotationMode=value;selectedInk=-1;Command(SelectId);}
    void SetAnnotationColor(uint32_t color){FinishInk();color&=0xffffff;if(Selected()||textTool)textColor=color;else if(ShapeTool()||(SelectedInk()&&SelectedInk()->filled))shapeColor=color;else penColor=color;Sync();if(Selected())ChangeStyle([&](auto& t){t.color=color;});if(SelectedInk()){auto before=document.scene;SelectedInk()->color=color;document.Commit(before);Publish();Sync();}}
    void SetAnnotationSize(int index){textSize=index==0?32:index==1?56:88;SetPenWidth(index==0?3:index==1?6:12);if(Selected())ChangeStyle([&](auto& t){t.size=textSize;});if(SelectedInk()){auto before=document.scene;SelectedInk()->width=penWidth;document.Commit(before);Publish();Sync();}}
    std::wstring CardName(){return FocusedCard()?FocusedCard()->name:L"";}
    void NewCardSelection(){focusedCard.clear();FinishInk();FinishEdit(true);selected=selectedCard=selectedImage=selectedInk=-1;textTool=false;drawTool=DrawTool::None;Sync();}
    CardRequestTarget GenerationTarget(){if(FlushCardState)FlushCardState();FinishInk();FinishEdit(true);return CaptureCardTarget(document,focusedCard);}
    bool TargetCurrent(const CardRequestTarget& target)const{return CardTargetCurrent(document,target);}
    std::optional<CardUsage> SelectedCardUsage(){if(auto card=FocusedCard())return card->usage;return {};}
    bool AcceptGenerated(const CardRequestTarget& target,const std::wstring& html,const std::wstring& name,const std::vector<CardAsset>& assets={},const std::optional<CardUsage>& usage={}){
        if(FlushCardState)FlushCardState();FinishInk();FinishEdit(true);if(!ApplyGeneratedCard(document,target,html,name,assets,usage))return false;
        if(target.creating&&focusedCard.empty()){selectedCard=static_cast<int>(document.scene.cards.size())-1;focusedCard=target.card.id;}
        Publish();Sync();return true;
    }

    void AddSetupCard(CardElement card){
        FinishInk();FinishEdit(true);
        if(document.scene.cards.size()>=12||ElementCount(document.scene)>=100)throw std::runtime_error("This scene has reached its card limit.");
        auto before=document.scene;document.scene.cards.push_back(std::move(card));
        selected=selectedImage=selectedInk=-1;selectedCard=static_cast<int>(document.scene.cards.size())-1;
        focusedCard=document.scene.cards.back().id;textTool=false;drawTool=DrawTool::None;
        document.Commit(before);playing.push_back(document.scene.cards.back());Publish();Sync();
    }
    void UpdateSetupCard(const std::wstring& id,const std::wstring& html){
        auto target=CaptureCardTarget(document,id);
        if(target.creating||target.card.html==html)return;
        AcceptGenerated(target,html,target.card.name);
    }
    void AddSampleCard(){
        FinishInk();drawTool=DrawTool::None;FinishEdit(true);if(document.scene.cards.size()>=12||ElementCount(document.scene)>=100)throw std::runtime_error("This scene has reached its card limit.");
        auto before=document.scene;auto c=SampleCard(!document.scene.cards.empty());
        double offset=.025*(document.scene.cards.size()%4);c.x=std::min(1.-c.width,c.x+offset);c.y=std::min(1.-c.height,c.y+offset);
        document.scene.cards.push_back(c);selected=selectedImage=-1;selectedCard=static_cast<int>(document.scene.cards.size())-1;focusedCard=document.scene.cards.back().id;textTool=false;
        document.Commit(before);Publish();Sync();SetFocus(window);
    }
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
        DragAcceptFiles(window,TRUE);
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
    ~TextEditorUI(){DragAcceptFiles(window,FALSE);if(editFont)DeleteObject(editFont);ClearEditBackground();}
    bool IsEditing()const{return edit!=nullptr;}
    std::shared_ptr<Frame> EditingFrame(const Frame& background){return ComposeText(background,*editingOverlay);}
    void RefreshEdit(){if(edit)InvalidateRect(edit,nullptr,FALSE);}
    void SetStage(RECT next){FinishInk();stage=next;PositionEdit();}
    bool MayDiscard(){
        if(FlushCardState)FlushCardState();
        FinishInk();FinishEdit(true);if(!document.Dirty())return true;
        int answer=MessageBoxW(window,L"Save your changes before continuing?",L"Callayers",MB_YESNOCANCEL|MB_ICONQUESTION);
        if(answer==IDCANCEL)return false;if(answer==IDYES)return Save();return true;
    }
    bool Message(UINT msg,WPARAM wp,LPARAM lp,LRESULT& result){
        result=0;
        const bool pointerMessage=msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK||msg==WM_LBUTTONUP||msg==WM_MOUSEMOVE||msg==WM_MOUSEWHEEL;
        if((msg==WM_CAPTURECHANGED||msg==WM_CANCELMODE)&&cardPointerCaptured){cardPointerCaptured=false;if(CardMouse)CardMouse(WM_LBUTTONUP,interactionPoint,0);}
        if(msg==WM_MOUSELEAVE&&cardPointerHover){cardPointerHover=false;if(CardMouse)CardMouse(msg,interactionPoint,0);}
        if(pointerMessage&&!dragging&&!marking&&drawTool==DrawTool::None&&!textTool){
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(msg==WM_MOUSEWHEEL)ScreenToClient(window,&p);
            const bool down=msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK;
            const int handle=HitHandle(p);const bool move=HitCardMove(p);
            // Native handles always win. The interior belongs to HTML, including
            // text selection, sliders, and other drag gestures implemented by the card.
            if(down&&!cardPointerCaptured&&(move||(CardOnStage()&&handle>=0))){
                FinishEdit(true);interacting=false;pendingSurrogate=0;dragHandle=move?-1:handle;
                beforeDrag=document.scene;dragStart=p;dragging=true;SetFocus(window);SetCapture(window);return true;
            }
            const int hit=PtInRect(&stage,p)?HitCard(p):-1;
            const bool nativeTarget=move||handle>=0||AnnotationHit(p);
            if(cardPointerCaptured||(hit>=0&&!nativeTarget)){
                if(down){FinishEdit(true);interacting=true;pendingSurrogate=0;selected=selectedImage=selectedInk=-1;selectedCard=hit;focusedCard=document.scene.cards[hit].id;Sync();SetFocus(window);cardPointerCaptured=true;SetCapture(window);}
                if(msg==WM_MOUSEMOVE){TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);}
                POINT mapped{LONG(double(p.x-stage.left)*Width/std::max(1L,stage.right-stage.left)),LONG(double(p.y-stage.top)*Height/std::max(1L,stage.bottom-stage.top))};
                interactionPoint=mapped;cardPointerHover=true;if(CardMouse)CardMouse(msg,mapped,wp);
                if(msg==WM_LBUTTONUP){cardPointerCaptured=false;ReleaseCapture();}return true;
            }
            if(cardPointerHover){cardPointerHover=false;if(CardMouse)CardMouse(WM_MOUSELEAVE,interactionPoint,0);}
            if(down){interacting=false;pendingSurrogate=0;}
        }
        if(interacting){
            if((msg==WM_KEYDOWN||msg==WM_KEYUP)&&GetFocus()==window){
                if(wp==VK_ESCAPE){if(msg==WM_KEYDOWN)SetInteract(false);return true;}
                if(msg==WM_KEYDOWN&&wp=='V'&&(GetKeyState(VK_CONTROL)&0x8000)){
                    if(OpenClipboard(window)){auto data=GetClipboardData(CF_UNICODETEXT);if(data){auto text=static_cast<const wchar_t*>(GlobalLock(data));if(text){std::wstring value(text);GlobalUnlock(data);if(value.size()<=65536&&CardText)CardText(value);}}CloseClipboard();}return true;
                }
                if(CardKey)CardKey(msg,wp,lp);return true;
            }
            if(msg==WM_CHAR&&GetFocus()==window){
                if(GetKeyState(VK_CONTROL)&0x8000)return true;wchar_t ch=static_cast<wchar_t>(wp);
                if(ch>=0xd800&&ch<=0xdbff){pendingSurrogate=ch;return true;}
                std::wstring text;if(pendingSurrogate&&ch>=0xdc00&&ch<=0xdfff)text+=pendingSurrogate;pendingSurrogate=0;
                if(ch>=32||ch==L'\r'){text+=ch==L'\r'?L'\n':ch;if(CardText)CardText(text);}return true;
            }
        }
        if(msg==WM_DROPFILES){auto drop=reinterpret_cast<HDROP>(wp);struct FinishDrop {HDROP value;~FinishDrop(){DragFinish(value);}} cleanup{drop};POINT point{};DragQueryPoint(drop,&point);auto files=DropPaths(drop);ImportImages(files,point);return true;}
        if(msg==WM_APP+20){FinishEdit(wp!=0);SetFocus(window);return true;}
        if(msg==WM_APP+21){if(edit&&GetFocus()!=edit)FinishEdit(true);return true;}
        if(msg==WM_CTLCOLOREDIT&&reinterpret_cast<HWND>(lp)==edit&&Selected()){
            HDC dc=reinterpret_cast<HDC>(wp);auto color=Selected()->color;SetTextColor(dc,RGB((color>>16)&255,(color>>8)&255,color&255));SetBkMode(dc,TRANSPARENT);result=reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));return true;
        }
        if(msg==WM_COMMAND){
            int id=LOWORD(wp),notification=HIWORD(wp);if(id<SelectId||id>EditId)return false;
            if(syncing)return true;
            if(id!=EditId)FinishInk();
            if(id==EditId){
                if(notification==EN_CHANGE&&edit&&Selected()){Selected()->text=ReadText(edit);renderer.FitHeight(*Selected());PositionEdit();Publish();}
                if(notification==EN_KILLFOCUS)PostMessageW(window,WM_APP+21,0,0);return true;
            }
            if(id==SizeId&&notification!=EN_KILLFOCUS)return true;
            if((id==FontId||id==AlignId)&&notification!=CBN_SELCHANGE)return true;
            if(id==SelectId||id==TextId){interacting=false;FinishEdit(true);drawTool=DrawTool::None;textTool=id==TextId;Sync();SetFocus(window);return true;}
            if(id==SaveId){Save();return true;}if(id==OpenId){Open();return true;}
            if(id==NewIdControl){FinishEdit(true);if(MayDiscard()){interacting=false;laser.clear();playing.clear();focusedCard.clear();document.Loaded({});path.clear();selected=selectedCard=selectedImage=selectedInk=-1;Publish();Sync();}return true;}
            if(id==UndoId||id==RedoId){FinishEdit(true);if(id==UndoId)document.Undo();else document.Redo();selected=selectedCard=selectedImage=selectedInk=-1;Publish();Sync();return true;}
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
        if((msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK)&&drawTool!=DrawTool::None){
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(!PtInRect(&stage,p))return false;FinishInk();FinishEdit(true);SetFocus(window);lastInk=StagePoint(p);
            if(drawTool==DrawTool::Pen||ShapeTool()){
                inkPointCount=0;for(const auto& stroke:document.scene.ink)inkPointCount+=stroke.points.size();
                if(document.scene.ink.size()>=MaxInkStrokes||inkPointCount+(ShapeTool()?65:1)>MaxInkPoints)throw std::runtime_error("Drawing limit reached. Erase some strokes before adding more.");
            }
            beforeInk=document.scene;
            if(drawTool==DrawTool::Pen||ShapeTool()){shapeOrigin=lastInk;InkStroke stroke;stroke.id=NewId();stroke.color=ShapeTool()?shapeColor:penColor;stroke.filled=ShapeTool();stroke.width=penWidth;stroke.points={lastInk};document.scene.ink.push_back(std::move(stroke));++inkPointCount;}
            marking=true;SetCapture(window);InkMove(p,true);TickInk();return true;
        }
        if(msg==WM_MOUSEMOVE&&marking){InkMove({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return true;}
        if((msg==WM_LBUTTONUP||msg==WM_CAPTURECHANGED||msg==WM_CANCELMODE)&&marking){if(msg==WM_LBUTTONUP)InkMove({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});FinishInk();return true;}
        if(msg==WM_LBUTTONDOWN){
            FinishEdit(true);POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(!PtInRect(&stage,p))return false;SetFocus(window);
            if(textTool){
                if(ElementCount(document.scene)>=100)throw std::runtime_error("This scene has reached the 100-element limit.");
                selectedCard=selectedImage=selectedInk=-1;Scene before=document.scene;TextElement t;t.id=NewId();t.color=textColor;t.size=textSize;t.order=NextForegroundOrder(document.scene);t.x=std::clamp(double(p.x-stage.left)/(stage.right-stage.left),0.,.5);t.y=std::clamp(double(p.y-stage.top)/(stage.bottom-stage.top),0.,.8);
                renderer.FitHeight(t);document.scene.texts.push_back(t);selected=static_cast<int>(document.scene.texts.size())-1;textTool=false;Publish();Sync();BeginEdit(before);return true;
            }
            dragHandle=HitHandle(p);if(dragHandle<0)HitSelection(p);Sync();
            if(HasSelection()){beforeDrag=document.scene;dragStart=p;dragging=true;SetCapture(window);}return true;
        }
        if(msg==WM_LBUTTONDBLCLK){FinishEdit(true);POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};HitSelection(p);Sync();if(Selected())BeginEdit(document.scene);return true;}
        if(msg==WM_MOUSEMOVE&&dragging&&HasSelection()){
            double dx=double(GET_X_LPARAM(lp)-dragStart.x)/std::max(1L,stage.right-stage.left),dy=double(GET_Y_LPARAM(lp)-dragStart.y)/std::max(1L,stage.bottom-stage.top);
            if(SelectedInk()){
                auto original=beforeDrag.ink[selectedInk];auto bounds=Bounds(original);
                double sx=1,sy=1,tx=dx,ty=dy;
                if(dragHandle>=0){bool left=dragHandle==0||dragHandle==3||dragHandle==4,top=dragHandle==0||dragHandle==1;
                    sx=std::max(.05,(bounds.width+(left?-dx:dx))/bounds.width);sy=dragHandle>=4?1:std::max(.05,(bounds.height+(top?-dy:dy))/bounds.height);
                    tx=left?bounds.width*(1-sx):0;ty=top?bounds.height*(1-sy):0;
                }else{tx=std::clamp(dx,-bounds.x,1-bounds.x-bounds.width);ty=std::clamp(dy,-bounds.y,1-bounds.y-bounds.height);}
                for(auto& point:original.points){point.x=std::clamp(bounds.x+(point.x-bounds.x)*sx+tx,0.,1.);point.y=std::clamp(bounds.y+(point.y-bounds.y)*sy+ty,0.,1.);}
                *SelectedInk()=std::move(original);Publish(false);return true;
            }
            if(SelectedCard()){auto& original=beforeDrag.cards[selectedCard];*SelectedCard()=dragHandle<0?MoveCard(original,dx,dy):ResizeCard(original,dragHandle,dx,dy);PublishCards();InvalidateRect(window,nullptr,FALSE);return true;}
            if(SelectedImage()){auto& original=beforeDrag.images[selectedImage];*SelectedImage()=dragHandle<0?MoveImage(original,dx,dy):ResizeImage(original,dragHandle,dx,dy);Publish();return true;}
            auto& original=beforeDrag.texts[selected];*Selected()=dragHandle<0?MoveText(original,dx,dy):ResizeText(original,dragHandle,dx,dy);if(dragHandle>=0)renderer.FitHeight(*Selected());Publish();return true;
        }
        if((msg==WM_LBUTTONUP||msg==WM_CAPTURECHANGED)&&dragging){dragging=false;document.Commit(beforeDrag);ReleaseCapture();Sync();return true;}
        if(msg==WM_SETCURSOR&&LOWORD(lp)==HTCLIENT){
            POINT p;GetCursorPos(&p);ScreenToClient(window,&p);if(PtInRect(&stage,p)||HitCardMove(p)){
                int handle=HitHandle(p);LPCWSTR cursor=drawTool!=DrawTool::None?IDC_CROSS:HitCardMove(p)?IDC_SIZEALL:handle>=4?IDC_SIZEWE:(handle==0||handle==2)?IDC_SIZENWSE:(handle==1||handle==3)?IDC_SIZENESW:textTool?IDC_IBEAM:IDC_ARROW;
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
        if(GetFocus()&&(GetFocus()==Control(SizeId)||GetFocus()==Control(FontId)||GetFocus()==Control(AlignId)))return false;
        if(control&&key=='V')return PasteImage();
        if(control&&(key=='Z'||key=='Y')){FinishInk();if(key=='Y'||(GetKeyState(VK_SHIFT)&0x8000))document.Redo();else document.Undo();selected=selectedCard=selectedImage=selectedInk=-1;Publish();Sync();return true;}
        if(key==VK_ESCAPE){FinishInk(false);if(!laser.empty()){laser.clear();Publish(false);}if(dragging){dragging=false;document.scene=beforeDrag;ReleaseCapture();Publish();}selected=selectedCard=selectedImage=selectedInk=-1;textTool=false;drawTool=DrawTool::None;Sync();return true;}
        if(key==VK_DELETE){Delete();return true;}
        if(key==VK_F2&&Selected()){BeginEdit(document.scene);return true;}
        if(!control&&key=='T'){FinishInk();drawTool=DrawTool::None;textTool=true;Sync();return true;}
        if(!control&&key=='P'){SetDrawingTool(DrawTool::Pen);return true;}
        return false;
    }
    void PaintAdorners(HDC dc){
        if(!HasSelection())return;auto r=SelectionRect();
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
        if(CardOnStage()){
            auto grip=CardMoveRect();auto brush=CreateSolidBrush(RGB(76,69,116));FillRect(dc,&grip,brush);DeleteObject(brush);
            auto font=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));auto color=SetTextColor(dc,RGB(255,255,255));auto mode=SetBkMode(dc,TRANSPARENT);
            DrawTextW(dc,L"Move",-1,&grip,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SetBkMode(dc,mode);SetTextColor(dc,color);SelectObject(dc,font);
        }
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
