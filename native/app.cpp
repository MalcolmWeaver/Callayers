#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <mferror.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include "capture.h"
#include "overlay.h"
#include "frame_bridge.h"
#include "resource_diagnostics.h"
#include "workspace.h"
#include "camera_preferences.h"
#include "annotations.h"
#include "annotation_store.h"
#include <Microsoft.UI.Dispatching.Interop.h>
#include <winrt/base.h>

using namespace callayers;
namespace {
struct Card {
    double x, y, w, h;
    bool visible = true;
    // The HTML document keeps this layout size for its whole lifetime. Resizing
    // changes presentation scale, so scripts/animation and text layout survive.
    double contentW = w, contentH = h;
};
struct PresentedFrame { std::vector<uint8_t> pixels; bool live = false; ULONGLONG tick = 0; unsigned version = 0; };
struct App {
    HWND window = nullptr;
    std::unique_ptr<Workspace> workspace;
    HFONT font = nullptr, titleFont = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(29, 29, 33));
    HDC backDC = nullptr; HBITMAP backBitmap = nullptr; HGDIOBJ oldBitmap = nullptr;
    int backWidth = 0, backHeight = 0;
    FrameBridge bridge;
    Overlay overlay;
    std::vector<CameraDevice> devices;
    int selectedCamera = -1;
    std::mutex commandMutex, previewMutex;
    std::wstring wantedIdentity;
    std::wstring wantedName;
    CameraPreferences preferences;
    CameraPreference rememberedCamera, savingCamera;
    unsigned preferenceRevision=0, preferenceAttemptedVersion=0;
    HRESULT preferenceLoadError=S_OK;
    bool preferenceSaveFailed=false;
    std::atomic<unsigned> commandVersion{1};
    std::atomic<bool> demand{false}, quitting{false};
    std::atomic<HRESULT> captureError{S_OK};
    std::atomic<bool> opening{false};
    std::thread worker;
    std::shared_ptr<const PresentedFrame> preview;
    Card cards[3]{{48, 540, 420, 128}, {870, 62, 340, 148}, {902, 470, 300, 180}};
    std::vector<Annotation> annotations;
    AnnotationStore annotationStore;
    AnnotationTool tool=AnnotationTool::Select;
    std::wstring inkColor=L"#ff0000";
    std::wstring shapeColor=L"#ffffff";
    int inkSize=1, selectedAnnotation=-1, hoveredAnnotation=-1;
    uint64_t nextAnnotationId=1, editingAnnotationId=0;
    Annotation editingTextDraft;
    Annotation originalInk;
    unsigned annotationRevision=0;
    HRESULT annotationRestoreError=S_OK;
    bool annotationSaveFailed=false, draggingAnnotation=false;
    bool dragging=false, resizing=false, drawing=false;
    int selectedCard = -1;
    InkPoint dragStart{}; Card original{};
    RECT previewRect{};
    float dpi = 1;
    std::wstring status;
    int px(double value) const { return int(std::lround(value * dpi)); }
    ~App() {
        if (oldBitmap) SelectObject(backDC, oldBitmap);
        if (backBitmap) DeleteObject(backBitmap);
        if (backDC) DeleteDC(backDC);
        if (font) DeleteObject(font); if (titleFont) DeleteObject(titleFont); DeleteObject(background);
    }
    void invalidateOutput() { bridge.stop(); }
    std::wstring scene() const {
        std::wostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(2) << L"{\"cards\":[";
        for (int i = 0; i < 3; ++i) {
            if (i) out << ',';
            const auto& c = cards[i];
            out << L"{\"id\":" << i << L",\"x\":" << c.x << L",\"y\":" << c.y << L",\"w\":" << c.w << L",\"h\":" << c.h << L",\"contentW\":" << c.contentW << L",\"contentH\":" << c.contentH << L",\"visible\":" << (c.visible ? L"true" : L"false") << '}';
        }
        out << L"],\"annotations\":" << AnnotationJson(annotations) << '}'; return out.str();
    }
    void syncCards() { if(workspace) { bool visible[3]{cards[0].visible,cards[1].visible,cards[2].visible}; workspace->card(selectedCard,visible); } }
    void changed() { overlay.update(scene()); syncCards(); InvalidateRect(window, nullptr, FALSE); }
    void enumerate(bool initial = false) {
        if(initial) {
            rememberedCamera=preferences.load(preferenceLoadError);
            std::lock_guard lock(commandMutex);wantedIdentity=rememberedCamera.identity;wantedName=rememberedCamera.name;
        }
        std::wstring previous; { std::lock_guard lock(commandMutex); previous=wantedIdentity; }
        devices = EnumerateCameras(); selectedCamera = -1;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].identity == previous) selectedCamera = int(i);
        }
        choose();
    }
    void choose() {
        invalidateOutput();
        { std::lock_guard lock(previewMutex); preview.reset(); }
        // An absent device retains the requested identity. Only explicit camera
        // selection changes it; Retry never substitutes another enumerated item.
        if(selectedCamera>=0) {
            std::lock_guard lock(commandMutex);wantedIdentity=devices[selectedCamera].identity;wantedName=devices[selectedCamera].name;
            preferenceLoadError=S_OK;
        }
        preferenceSaveFailed=false;
        std::vector<std::wstring> names;for(const auto& device:devices)names.push_back(device.name);
        if(workspace)workspace->cameras(names,selectedCamera,selectedCamera<0?wantedName:L"");
        captureError = S_OK; ++commandVersion;
        updateDemand();
    }
    void rememberWorkingCamera(bool fresh) {
        if(preferenceRevision) {
            auto result=preferences.result();
            if(!result.saved.identity.empty())rememberedCamera=result.saved;
            if(result.revision==preferenceRevision) {
                preferenceSaveFailed=FAILED(result.error) && savingCamera.identity==wantedIdentity;
                preferenceRevision=0;
            }
        }
        unsigned version=commandVersion;
        if(!fresh || preferenceAttemptedVersion==version)return;
        preferenceAttemptedVersion=version;
        // A pending write for a different choice must also be superseded when
        // the user returns to the earlier saved camera (A -> B -> A).
        if(!preferenceRevision && rememberedCamera.identity==wantedIdentity && rememberedCamera.name==wantedName)return;
        savingCamera={wantedIdentity,wantedName};preferenceSaveFailed=false;
        preferenceRevision=preferences.save(savingCamera);
    }
    void updateDemand() {
        bool wanted = selectedCamera >= 0 && (!IsIconic(window) || bridge.consumerActive());
        bool was = demand.exchange(wanted);
        if (was && !wanted) invalidateOutput();
        overlay.enable(wanted);
    }
    void buildFonts() {
        if (font) DeleteObject(font); if (titleFont) DeleteObject(titleFont);
        font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        titleFont = CreateFontW(-px(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    void layout() {
        if(workspace && workspace->editingText())workspace->finishText();
        RECT client; GetClientRect(window, &client);
        int left = px(324), top = px(96+(workspace?workspace->toolbarHeight():0)), availableW = std::max<int>(1, client.right - left - px(24)), availableH = std::max<int>(1, client.bottom - top - px(24));
        double scale = std::min(double(availableW) / Width, double(availableH) / Height);
        int w = int(Width * scale), h = int(Height * scale);
        previewRect = {left + (availableW-w)/2, top + (availableH-h)/2, left + (availableW+w)/2, top + (availableH+h)/2};
        if(workspace)workspace->layout(client.right,client.bottom,dpi,previewRect);
        InvalidateRect(window, nullptr, FALSE);
    }
    InkPoint canvasPoint(int x, int y) const {
        return {std::clamp(double(x - previewRect.left) * Width / std::max(1L, previewRect.right-previewRect.left), 0.0, double(Width)),
                std::clamp(double(y - previewRect.top) * Height / std::max(1L, previewRect.bottom-previewRect.top), 0.0, double(Height))};
    }
    RECT cardRect(const Card& c) const {
        double sx = double(previewRect.right-previewRect.left) / Width, sy = double(previewRect.bottom-previewRect.top) / Height;
        return {previewRect.left + int(c.x*sx), previewRect.top + int(c.y*sy), previewRect.left+int((c.x+c.w)*sx), previewRect.top+int((c.y+c.h)*sy)};
    }
    RECT inkRect(InkBounds b) const {
        double sx=double(previewRect.right-previewRect.left)/Width,sy=double(previewRect.bottom-previewRect.top)/Height;
        return {previewRect.left+int(b.x*sx),previewRect.top+int(b.y*sy),previewRect.left+int((b.x+b.w)*sx),previewRect.top+int((b.y+b.h)*sy)};
    }
    double hitTolerance() const {return double(px(7))*Width/std::max(1L,previewRect.right-previewRect.left);}
    int annotationAt(InkPoint p) const {for(int i=int(annotations.size())-1;i>=0;--i)if(HitAnnotation(annotations[i],p,hitTolerance()))return i;return -1;}
    static bool isShape(AnnotationTool type) {return type>=AnnotationTool::Rectangle && type<=AnnotationTool::Arrow;}
    const std::wstring& drawingColor() const {return isShape(tool)?shapeColor:inkColor;}
    void syncAnnotationControls() {
        if(!workspace)return;
        if(selectedAnnotation>=0 && selectedAnnotation<int(annotations.size()))workspace->annotationControls(tool,annotations[selectedAnnotation].color,annotations[selectedAnnotation].size);
        else workspace->annotationControls(tool,drawingColor(),inkSize);
    }
    bool restoreAnnotations(bool merge=false) {
        HRESULT error=S_OK;auto json=annotationStore.load(error);std::vector<Annotation> restored;
        if(SUCCEEDED(error))try {restored=ParseAnnotations(json);}catch(...){error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);}
        annotationRestoreError=error;if(FAILED(error))return false;
        uint64_t next=1;for(const auto& a:restored)next=std::max(next,a.id+1);
        if(merge) {
            size_t points=0;for(const auto& a:restored)points+=a.points.size();for(const auto& a:annotations)points+=a.points.size();
            if(restored.size()+annotations.size()>MaxAnnotations || points>MaxInkPoints){annotationRestoreError=E_FAIL;return false;}
            for(auto a:annotations){a.id=next++;restored.push_back(std::move(a));}
        }
        annotations=std::move(restored);nextAnnotationId=next;selectedAnnotation=hoveredAnnotation=-1;annotationSaveFailed=false;return true;
    }
    void saveAnnotations() {
        if(FAILED(annotationRestoreError))return; // Never overwrite a workspace we could not read.
        annotationRevision=annotationStore.save(AnnotationJson(annotations));annotationSaveFailed=false;
    }
    void eraseAnnotation(int index) {
        if(index<0 || index>=int(annotations.size()))return;
        annotations.erase(annotations.begin()+index);selectedAnnotation=hoveredAnnotation=-1;changed();syncAnnotationControls();saveAnnotations();
    }
    bool measureText(Annotation& a) {
        HDC dc=GetDC(window);HFONT textFont=CreateFontW(-int(std::round(a.font)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto old=SelectObject(dc,textFont);RECT measured{0,0,int(std::max(120.0,a.box.w)),0};
        DrawTextW(dc,a.text.c_str(),int(a.text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
        SelectObject(dc,old);DeleteObject(textFont);ReleaseDC(window,dc);
        double h=std::max(a.font*1.3,double(measured.bottom)*1.25+4),w=std::max(120.0,double(measured.right));
        if(h>Height || w>Width){MessageBoxW(window,L"This text is too large for the picture. Shorten it or choose a smaller size.",L"Text doesn't fit",MB_OK|MB_ICONINFORMATION);return false;}
        a.box.w=w;a.box.h=h;a.box.x=std::clamp(a.box.x,0.0,Width-w);a.box.y=std::clamp(a.box.y,0.0,Height-h);return true;
    }
    bool commitText(std::wstring value,bool commit,double height) {
        if(!commit){editingAnnotationId=0;return true;}
        auto found=std::find_if(annotations.begin(),annotations.end(),[&](const auto& a){return a.id==editingAnnotationId;});
        bool empty=value.empty() || std::all_of(value.begin(),value.end(),[](wchar_t c){return std::iswspace(c)!=0;});
        if(empty){if(found!=annotations.end())eraseAnnotation(int(found-annotations.begin()));editingAnnotationId=0;return true;}
        Annotation a=editingTextDraft;
        if(found==annotations.end()){if(annotations.size()>=MaxAnnotations){MessageBoxW(window,L"There are too many annotations. Remove a few to continue.",L"Annotations",MB_OK);return false;}a.id=nextAnnotationId;}
        if(!std::isfinite(height) || height<=0 || height>Height-a.box.y){MessageBoxW(window,L"This text runs past the bottom of the picture. Shorten it before finishing.",L"Text doesn't fit",MB_OK|MB_ICONINFORMATION);return false;}
        a.text=std::move(value);a.box.h=height;
        if(found!=annotations.end()){selectedAnnotation=int(found-annotations.begin());*found=std::move(a);}
        else {++nextAnnotationId;annotations.push_back(std::move(a));selectedAnnotation=int(annotations.size())-1;}
        editingAnnotationId=0;changed();syncAnnotationControls();saveAnnotations();return true;
    }
    void beginText(InkPoint p,int existing=-1) {
        if(workspace && !workspace->finishText())return;
        finishGesture();
        editingAnnotationId=existing>=0?annotations[existing].id:0;
        if(existing>=0)editingTextDraft=annotations[existing];
        else {editingTextDraft=Annotation{};editingTextDraft.kind=AnnotationTool::Text;editingTextDraft.color=inkColor;editingTextDraft.size=inkSize;editingTextDraft.font=TextSizes[inkSize];editingTextDraft.box={std::clamp(p.x,0.0,Width-480.0),std::clamp(p.y,0.0,Height-editingTextDraft.font*1.2),480,editingTextDraft.font*1.2};}
        selectedAnnotation=existing;
        selectedCard=-1;syncCards();syncAnnotationControls();
        auto at=inkRect(editingTextDraft.box);double scale=double(previewRect.right-previewRect.left)/Width;
        at.right=at.left+int(std::ceil(editingTextDraft.box.w*scale));at.bottom=previewRect.bottom;
        workspace->editText(at,editingTextDraft.text,editingTextDraft.font,editingTextDraft.color,editingTextDraft.box.w,scale,editingTextDraft.box.h);
        InvalidateRect(window,nullptr,FALSE);
    }
    void setTool(AnnotationTool chosen) {
        finishGesture();if(workspace && !workspace->finishText()){syncAnnotationControls();return;}
        tool=chosen;hoveredAnnotation=-1;if(tool!=AnnotationTool::Select)selectedCard=-1;
        if(isShape(tool))selectedAnnotation=-1;
        syncCards();syncAnnotationControls();InvalidateRect(window,nullptr,FALSE);
    }
    void setInkColor(std::wstring color) {
        finishGesture();if(workspace && !workspace->finishText()){syncAnnotationControls();return;}
        if(isShape(tool) || (selectedAnnotation>=0 && isShape(annotations[selectedAnnotation].kind)))shapeColor=color;
        else inkColor=color;
        if(selectedAnnotation>=0){annotations[selectedAnnotation].color=std::move(color);changed();saveAnnotations();}
        syncAnnotationControls();
    }
    void setInkSize(int size) {
        if(size<0 || size>2)return;
        finishGesture();if(workspace && !workspace->finishText()){syncAnnotationControls();return;}
        inkSize=size;
        if(selectedAnnotation>=0){auto before=annotations[selectedAnnotation];auto& a=annotations[selectedAnnotation];a.size=size;
            if(a.kind==AnnotationTool::Text){a.font=TextSizes[size];if(!measureText(a)){a=before;syncAnnotationControls();return;}}
            changed();saveAnnotations();}
        syncAnnotationControls();
    }
    void finishGesture(bool cancel=false) {
        bool edited=drawing || draggingAnnotation;
        if(cancel && edited && selectedAnnotation>=0){if(drawing)annotations.erase(annotations.begin()+selectedAnnotation);else annotations[selectedAnnotation]=originalInk;selectedAnnotation=-1;}
        else if(drawing && selectedAnnotation>=0){const auto& a=annotations[selectedAnnotation];auto b=Bounds(a);if((a.kind==AnnotationTool::Rectangle || a.kind==AnnotationTool::Ellipse) && (b.w<3 || b.h<3)){annotations.erase(annotations.begin()+selectedAnnotation);selectedAnnotation=-1;}}
        drawing=dragging=draggingAnnotation=resizing=false;
        if(edited){changed();syncAnnotationControls();if(!cancel)saveAnnotations();}
        if(GetCapture()==window)ReleaseCapture();
    }
    void pointerDown(int x,int y) {
        if(workspace && !workspace->finishText())return;
        POINT at{x,y};if(!PtInRect(&previewRect,at) || !demand || FAILED(captureError.load()))return;
        SetFocus(window);InkPoint p=canvasPoint(x,y);dragStart=p;
        if(tool==AnnotationTool::Text){int hit=annotationAt(p);beginText(p,hit>=0 && annotations[hit].kind==AnnotationTool::Text?hit:-1);return;}
        if(tool==AnnotationTool::Eraser){eraseAnnotation(annotationAt(p));return;}
        if(tool!=AnnotationTool::Select) {
            size_t count=0;for(const auto& a:annotations)count+=a.points.size();
            if(annotations.size()>=MaxAnnotations || count>=MaxInkPoints-2){MessageBoxW(window,L"There are too many annotations. Remove a few to continue.",L"Annotations",MB_OK);return;}
            Annotation a;a.id=nextAnnotationId++;a.kind=tool;a.color=drawingColor();a.size=inkSize;a.box={p.x,p.y,0,0};
            if(tool==AnnotationTool::Pen || tool==AnnotationTool::Line || tool==AnnotationTool::Arrow)a.points={p,{std::min(double(Width),p.x+0.1),p.y}};
            annotations.push_back(std::move(a));selectedAnnotation=int(annotations.size())-1;selectedCard=-1;drawing=true;
        }else {
            // Private handles are hit before the painted objects, especially an
            // arrow's endpoint and a stroke's empty lower-right bounding corner.
            bool handle=false;
            if(selectedAnnotation>=0){const auto& a=annotations[selectedAnnotation];auto b=Bounds(a);InkPoint end{b.x+b.w,b.y+b.h};if((a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow)&&a.points.size()==2)end=a.points[1];handle=std::hypot(p.x-end.x,p.y-end.y)<=hitTolerance()*1.8;}
            int hit=handle?selectedAnnotation:annotationAt(p);
            selectedAnnotation=hit;
            if(hit>=0){selectedCard=-1;originalInk=annotations[hit];draggingAnnotation=true;resizing=handle;}
            else {selectedCard=-1;for(int i=2;i>=0;--i){const auto& c=cards[i];if(c.visible && p.x>=c.x && p.x<=c.x+c.w && p.y>=c.y && p.y<=c.y+c.h){selectedCard=i;original=c;dragging=true;resizing=p.x>c.x+c.w-28 && p.y>c.y+c.h-28;break;}}}
        }
        hoveredAnnotation=-1;SetCapture(window);changed();syncAnnotationControls();
    }
    void pointerMove(int x,int y) {
        InkPoint p=canvasPoint(x,y);
        if(tool==AnnotationTool::Eraser){POINT at{x,y};int hover=PtInRect(&previewRect,at)?annotationAt(p):-1;if(hover!=hoveredAnnotation){hoveredAnnotation=hover;InvalidateRect(window,nullptr,FALSE);}TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);return;}
        if(drawing && selectedAnnotation>=0) {
            auto& a=annotations[selectedAnnotation];
            if(a.kind==AnnotationTool::Pen){if(a.points.size()>=4096)return;size_t count=0;for(const auto& item:annotations)count+=item.points.size();if(count>=MaxInkPoints)return;auto last=a.points.back();if(std::hypot(p.x-last.x,p.y-last.y)<2)return;a.points.push_back(p);}
            else if(a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow)a.points[1]=p;
            else a.box={std::min(p.x,dragStart.x),std::min(p.y,dragStart.y),std::abs(p.x-dragStart.x),std::abs(p.y-dragStart.y)};
        }else if(draggingAnnotation && selectedAnnotation>=0) {
            if(resizing)ResizeAnnotation(annotations[selectedAnnotation],originalInk,p);
            else MoveAnnotation(annotations[selectedAnnotation],originalInk,p.x-dragStart.x,p.y-dragStart.y);
        }else if(dragging && selectedCard>=0) {
            auto& c=cards[selectedCard];
            if(resizing){double factor=1+((p.x-dragStart.x)*original.w+(p.y-dragStart.y)*original.h)/(original.w*original.w+original.h*original.h);double maximum=std::min((Width-c.x)/original.w,(Height-c.y)/original.h),minimum=std::min(maximum,std::max(180.0/original.w,100.0/original.h));factor=std::clamp(factor,minimum,maximum);c.w=original.w*factor;c.h=original.h*factor;}
            else {c.x=std::clamp(original.x+p.x-dragStart.x,0.0,Width-c.w);c.y=std::clamp(original.y+p.y-dragStart.y,0.0,Height-c.h);}
        }else return;
        changed();
    }
    void doubleClick(int x,int y){POINT at{x,y};if(!PtInRect(&previewRect,at) || !demand || FAILED(captureError.load()))return;InkPoint p=canvasPoint(x,y);int hit=annotationAt(p);if((tool==AnnotationTool::Select || tool==AnnotationTool::Text)&&hit>=0&&annotations[hit].kind==AnnotationTool::Text){finishGesture();beginText(p,hit);}else pointerDown(x,y);}
    void text(HDC dc, const std::wstring& value, RECT rect, UINT flags = DT_LEFT | DT_WORDBREAK) { DrawTextW(dc, value.c_str(), -1, &rect, flags); }
    void paintAnnotationControls(HDC dc) {
        int index=tool==AnnotationTool::Eraser?hoveredAnnotation:tool==AnnotationTool::Select?selectedAnnotation:-1;
        if(index<0 || index>=int(annotations.size()))return;
        const auto& a=annotations[index];auto b=Bounds(a);RECT r=inkRect(b);
        bool erase=tool==AnnotationTool::Eraser;COLORREF color=erase?RGB(255,130,130):RGB(109,211,255);
        HPEN outline=CreatePen(erase?PS_DASH:PS_SOLID,erase?1:px(2),color);
        auto oldPen=SelectObject(dc,outline),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        Rectangle(dc,r.left-px(3),r.top-px(3),r.right+px(3),r.bottom+px(3));
        SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(outline);
        if(!erase){
            InkPoint corner{b.x+b.w,b.y+b.h};
            if((a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow)&&a.points.size()==2)corner=a.points[1];
            auto at=inkRect({corner.x,corner.y,0,0});RECT handle{at.left-px(6),at.top-px(6),at.left+px(6),at.top+px(6)};
            HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&handle,brush);DeleteObject(brush);
        }
    }
    void paint() {
        PAINTSTRUCT ps; HDC target = BeginPaint(window, &ps); RECT client; GetClientRect(window, &client);
        if (!backDC) backDC = CreateCompatibleDC(target);
        if (!backBitmap || backWidth != client.right || backHeight != client.bottom) {
            if (oldBitmap) SelectObject(backDC, oldBitmap);
            if (backBitmap) DeleteObject(backBitmap);
            backWidth = std::max(1L, client.right); backHeight = std::max(1L, client.bottom);
            backBitmap = CreateCompatibleBitmap(target, backWidth, backHeight); oldBitmap = SelectObject(backDC, backBitmap);
        }
        HDC dc = backDC; FillRect(dc, &client, background); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(237,241,249));
        std::shared_ptr<const PresentedFrame> frame;
        { std::lock_guard lock(previewMutex); frame = preview; }
        bool fresh = frame && frame->live && frame->version == commandVersion && GetTickCount64()-frame->tick < FreshMs && demand && SUCCEEDED(captureError.load());
        FillRect(dc, &previewRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (fresh) {
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=Width; info.bmiHeader.biHeight=-Height;
            info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
            StretchDIBits(dc, previewRect.left, previewRect.top, previewRect.right-previewRect.left, previewRect.bottom-previewRect.top,
                0,0,Width,Height,frame->pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            if (selectedCard >= 0 && cards[selectedCard].visible) {
                RECT r = cardRect(cards[selectedCard]); HPEN outline = CreatePen(PS_SOLID, px(2), RGB(109,211,255));
                auto oldPen = SelectObject(dc, outline), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, r.left, r.top, r.right, r.bottom);
                SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(outline);
                RECT handle{r.right-px(12),r.bottom-px(12),r.right,r.bottom}; HBRUSH b=CreateSolidBrush(RGB(109,211,255)); FillRect(dc,&handle,b); DeleteObject(b);
            }
            paintAnnotationControls(dc);
        }
        BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY); EndPaint(window,&ps);
    }
    void refreshWorkspace() {
        std::shared_ptr<const PresentedFrame> frame;
        { std::lock_guard lock(previewMutex); frame=preview; }
        HRESULT error=captureError.load();
        bool fresh=frame && frame->live && frame->version==commandVersion && GetTickCount64()-frame->tick<FreshMs && demand && SUCCEEDED(error) && !overlay.state->failed;
        rememberWorkingCamera(fresh);
        if(annotationRevision){auto saved=annotationStore.result();if(saved.revision==annotationRevision){annotationSaveFailed=FAILED(saved.error);annotationRevision=0;}}
        if(IsIconic(window))return; // Persist a working choice even in a minimized call, without updating hidden XAML.
        std::wstring message, state;
        if(selectedCamera<0) {
            if(!wantedIdentity.empty()) {state=L"Webcam not connected";message=rememberedCamera.identity==wantedIdentity?L"Your saved webcam isn't connected. Reconnect it and Retry, or choose another camera.":L"Your webcam isn't connected. Reconnect it and Retry, or choose another camera.";}
            else if(FAILED(preferenceLoadError)) {state=L"Choose your webcam";message=L"We couldn't read your saved webcam choice. Choose your webcam to remember it again.";}
            else {state=L"Choose your webcam";message=devices.empty()?L"Connect your webcam, then choose Retry.":L"Choose your webcam.\nYour camera and cards will appear here.";}
        }
        else if(overlay.state->failed) {state=L"Cards unavailable";message=L"We couldn't show your cards. Choose Retry to restart them.";}
        else if(error==E_ACCESSDENIED) {state=L"Camera access blocked";message=L"Camera access blocked.\nOpen Camera privacy settings. Turn on Camera access and Let desktop apps access your camera, then Retry.";}
        else if(error==HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)) {state=L"Camera in use";message=L"Camera in use.\nYour webcam is being used by another app. Close that app's camera preview or stop its video, then Retry.";}
        else if(FAILED(error)) {state=L"Camera not readable";message=L"Camera not readable.\nClose other apps using your webcam (the webcam light should turn off), then Retry.";}
        else if(!fresh) {state=L"Opening your webcam";message=opening?L"Opening your webcam…":L"Waiting for a picture. If it doesn't appear, choose Retry.";}
        else state=bridge.consumerActive()?L"Video is reaching your calling app":L"Ready for your call";
        if(fresh && preferenceSaveFailed)state=L"Couldn't remember webcam";
        if(fresh && annotationSaveFailed)state=L"Couldn't save annotations";
        if(fresh && FAILED(annotationRestoreError))state=L"Couldn't restore annotations";
        status=state;
        if(workspace)workspace->state(state,message,fresh,error==E_ACCESSDENIED,fresh && (preferenceSaveFailed || annotationSaveFailed || FAILED(annotationRestoreError)));
    }
    static void compose(const CameraImage& camera, const OverlayImage& overlay, std::vector<uint8_t>& output) {
        StageTimer timing(VideoStage::Compose);
        output.assign(PixelBytes,0);
        double scale=std::min(double(Width)/camera.width,double(Height)/camera.height);
        int w=std::max(1,int(camera.width*scale)), h=std::max(1,int(camera.height*scale));
        int left=(Width-w)/2, top=(Height-h)/2;
        for(int y=0;y<h;++y) {
            UINT sourceY=std::min(camera.height-1,UINT(y/scale));
            auto dst=output.data()+(size_t(y+top)*Width+left)*4;
            auto row=camera.pixels.data()+size_t(sourceY)*camera.width*4;
            if (w == int(camera.width)) memcpy(dst, row, size_t(w)*4);
            else for(int x=0;x<w;++x) { UINT sourceX=std::min(camera.width-1,UINT(x/scale)); memcpy(dst+x*4,row+sourceX*4,4); }
        }
        for(size_t i=0;i<PixelBytes;i+=4) {
            unsigned inverse=255-overlay.pixels[i+3];
            for(int c=0;c<3;++c) output[i+c]=uint8_t(std::min(255u,unsigned(overlay.pixels[i+c])+(unsigned(output[i+c])*inverse+127)/255));
            output[i+3]=255;
        }
    }
    void videoLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            Capture camera;
            unsigned version=0; bool started=false, attempted=false; LONG64 session=0;
            std::shared_ptr<OverlayImage> overlayFrame;
            auto next=std::chrono::steady_clock::now();
            while(!quitting) {
                unsigned requested=commandVersion;
                if(requested!=version || !demand) {
                    if(started || attempted) { bridge.stop(); camera.stop(); }
                    started=false; attempted=false; overlayFrame.reset(); version=requested;
                }
                if(demand && !attempted) {
                    std::wstring identity; {std::lock_guard lock(commandMutex);identity=wantedIdentity;}
                    attempted=true; opening=true;
                    HRESULT hr=identity.empty()?E_INVALIDARG:camera.start(identity);
                    if(FAILED(hr)) {std::ostringstream diagnostic;diagnostic<<"camera_open_failed hresult=0x"<<std::hex<<static_cast<unsigned long>(hr);Resources().event(diagnostic.str());}
                    opening=false;
                    if(version!=commandVersion || !demand || quitting) {camera.stop();attempted=false;continue;}
                    captureError=hr; started=SUCCEEDED(hr);
                    if(started) session=bridge.begin();
                }
                if(started && demand) {
                    if(FAILED(camera.state->error.load())) {
                        captureError=camera.state->error.load();
                        std::ostringstream diagnostic;diagnostic<<"camera_read_failed hresult=0x"<<std::hex<<static_cast<unsigned long>(captureError.load());Resources().event(diagnostic.str());
                        bridge.stop();camera.stop();started=false;
                    }
                    else {
                        std::vector<uint8_t> png; ULONGLONG tick=0; unsigned epoch=0;
                        {std::lock_guard lock(overlay.state->mutex);png.swap(overlay.state->png);tick=overlay.state->tick;epoch=overlay.state->epoch;}
                        if(!png.empty() && epoch==overlay.state->currentEpoch) {
                            auto decoded=DecodeOverlay(png,tick);
                            if(decoded && epoch==overlay.state->currentEpoch) overlayFrame=std::move(decoded);
                        }
                        std::shared_ptr<const CameraImage> image;
                        {std::lock_guard lock(camera.state->mutex);image=camera.state->latest;}
                        ULONGLONG now=GetTickCount64();
                        bool fresh=image && overlayFrame && now-image->tick<FreshMs && now-overlayFrame->tick<FreshMs && !overlay.state->failed;
                        if(fresh && version==commandVersion && demand && !quitting) {
                            auto result=std::make_shared<PresentedFrame>();
                            compose(*image,*overlayFrame,result->pixels); result->live=true; result->version=version;
                            result->tick=std::min(image->tick,overlayFrame->tick);
                            if(version==commandVersion && demand && !quitting) {
                                { StageTimer timing(VideoStage::Publish); bridge.publish(result->pixels,session,result->tick); }
                                std::lock_guard lock(previewMutex);preview=std::move(result);
                            }
                        } else {
                            // Let the last publication expire. A producer stall must
                            // not refresh its timestamp merely by recompositing it.
                            std::lock_guard lock(previewMutex);preview.reset();
                        }
                    }
                }
                next+=std::chrono::nanoseconds(1000000000/Fps);
                auto now=std::chrono::steady_clock::now();if(next<now)next=now;
                std::this_thread::sleep_until(next);
            }
            bridge.stop();camera.stop();
        }
        CoUninitialize();
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wParam,lParam);
    switch(message) {
    case WM_CREATE: {
        try {
            WorkspaceActions actions;
            actions.camera=[app](int index){if(index<0 || index>=int(app->devices.size()))return;app->selectedCamera=index;app->choose();};
            actions.selectCard=[app](int index){if(!app->workspace->finishText())return;app->finishGesture();app->selectedAnnotation=-1;app->selectedCard=index;app->setTool(AnnotationTool::Select);app->syncCards();};
            actions.visibility=[app](int index,bool show){app->cards[index].visible=show;app->changed();};
            actions.tool=[app](AnnotationTool tool){app->setTool(tool);};
            actions.color=[app](std::wstring color){app->setInkColor(std::move(color));};
            actions.size=[app](int size){app->setInkSize(size);};
            actions.text=[app](std::wstring text,bool commit,double height){return app->commitText(std::move(text),commit,height);};
            actions.retry=[app]{
                if(app->status==L"Couldn't restore annotations"){if(app->restoreAnnotations(!app->annotations.empty())){app->changed();app->syncAnnotationControls();app->saveAnnotations();}return;}
                if(app->status==L"Couldn't save annotations"){app->saveAnnotations();return;}
                if(app->preferenceSaveFailed && SUCCEEDED(app->captureError.load()) && app->demand && !app->overlay.state->failed) {
                    app->preferenceAttemptedVersion=0;app->preferenceSaveFailed=false;app->refreshWorkspace();return;
                }
                if(app->overlay.state->failed)app->overlay.retry(GetModuleHandleW(nullptr));app->enumerate();
            };
            actions.privacy=[] {ShellExecuteW(nullptr,L"open",L"ms-settings:privacy-webcam",nullptr,nullptr,SW_SHOWNORMAL);};
            actions.hideAll=[app]{for(auto& card:app->cards)card.visible=false;app->changed();};
            actions.clear=[app]{if(!app->workspace->finishText())return;app->finishGesture();if(MessageBoxW(app->window,L"Clear all annotations? This cannot be undone.",L"Clear annotations",MB_YESNO|MB_ICONQUESTION)==IDYES){app->annotations.clear();app->selectedAnnotation=app->hoveredAnnotation=-1;app->changed();app->syncAnnotationControls();app->saveAnnotations();}};
            actions.quit=[app]{PostMessageW(app->window,WM_CLOSE,0,0);};
            actions.layout=[app]{app->layout();};
            app->restoreAnnotations();
            app->workspace=std::make_unique<Workspace>(window,std::move(actions));
            app->dpi=GetDpiForWindow(window)/96.0f;app->buildFonts();app->layout();app->enumerate(true);app->syncCards();app->syncAnnotationControls();
            app->overlay.update(app->scene());app->overlay.initialize(GetModuleHandleW(nullptr));
            app->worker=std::thread([app]{app->videoLoop();});SetTimer(window,1,33,nullptr);return 0;
        } catch(winrt::hresult_error const& error) {Resources().event("workspace_controls_failed hresult="+std::to_string(error.code().value));MessageBoxW(window,L"Couldn't open the workspace. Close and reopen Callayers.",L"Callayers",MB_OK|MB_ICONERROR);return -1;}
        catch(...) {Resources().event("workspace_controls_failed");MessageBoxW(window,L"Couldn't open the workspace. Close and reopen Callayers.",L"Callayers",MB_OK|MB_ICONERROR);return -1;}
    }
    case WM_TIMER:app->updateDemand();app->overlay.capture();app->refreshWorkspace();if(!IsIconic(window))InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_SIZE:if(app->workspace){app->layout();app->updateDemand();}return 0;
    case WM_DPICHANGED: {
        app->dpi=HIWORD(wParam)/96.0f;app->buildFonts();auto r=reinterpret_cast<RECT*>(lParam);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->layout();return 0;
    }
    case WM_GETMINMAXINFO: {auto limits=reinterpret_cast<MINMAXINFO*>(lParam);limits->ptMinTrackSize={app->px(1000),app->px(760)};return 0;}
    case WM_KEYDOWN:
        if(wParam==VK_TAB && app->workspace){app->workspace->focus((GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
        if(wParam==VK_ESCAPE){app->finishGesture(true);return 0;}
        if(wParam==VK_DELETE){app->eraseAnnotation(app->selectedAnnotation);return 0;}break;
    case WM_LBUTTONDOWN:app->pointerDown(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_MOUSEMOVE:app->pointerMove(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_LBUTTONUP:app->finishGesture();return 0;
    case WM_CAPTURECHANGED:app->finishGesture();return 0;
    case WM_LBUTTONDBLCLK:app->doubleClick(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_MOUSELEAVE:app->hoveredAnnotation=-1;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_SETCURSOR:if(reinterpret_cast<HWND>(wParam)==window && LOWORD(lParam)==HTCLIENT){POINT at{};GetCursorPos(&at);ScreenToClient(window,&at);if(PtInRect(&app->previewRect,at)){SetCursor(LoadCursorW(nullptr,app->tool==AnnotationTool::Text?IDC_IBEAM:app->tool==AnnotationTool::Select?IDC_ARROW:IDC_CROSS));return TRUE;}}break;
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:SetTextColor(reinterpret_cast<HDC>(wParam),RGB(230,235,245));SetBkColor(reinterpret_cast<HDC>(wParam),RGB(18,22,31));return reinterpret_cast<LRESULT>(app->background);
    case WM_PAINT:app->paint();return 0;
    case WM_CLOSE:
        if(app->workspace && !app->workspace->finishText())return 0;
        app->finishGesture();
        if(app->bridge.consumerActive() && MessageBoxW(window,L"Quit Callayers and stop video in your calling app?",L"Stop video and quit",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
        app->quitting=true;app->demand=false;app->bridge.stop();KillTimer(window,1);app->overlay.enable(false);app->workspace.reset();
        DestroyWindow(window);return 0;
    case WM_DESTROY:app->quitting=true;app->bridge.stop();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    HANDLE single=CreateMutexW(nullptr,TRUE,L"Local\\Callayers.Foundation.v2.Desktop");
    if(!single || GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"Callayers is already open.",L"Callayers",MB_OK);if(single)CloseHandle(single);return 0;}
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com) || FAILED(MFStartup(MF_VERSION))){MessageBoxW(nullptr,L"Windows couldn't start the video services.",L"Callayers",MB_OK|MB_ICONERROR);CloseHandle(single);return 1;}
    InitCommonControls();
    int result=0;
    try {
        WorkspaceRuntime runtime;
        App app;
        if(!app.bridge.valid()){MessageBoxW(nullptr,L"Couldn't prepare Callayers video. Close and reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
        else {
            WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=L"CallayersFoundation";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
            HWND window=CreateWindowExW(0,wc.lpszClassName,L"Callayers",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1280,850,nullptr,nullptr,instance,&app);
            if(window){ShowWindow(window,show);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!ContentPreTranslateMessage(&message)){TranslateMessage(&message);DispatchMessageW(&message);}}}
            else {app.quitting=true;result=1;}
            if(app.worker.joinable())app.worker.join();
        }
    }
    catch(winrt::hresult_error const& error) {Resources().event("workspace_runtime_failed hresult="+std::to_string(error.code().value));MessageBoxW(nullptr,L"Couldn't start the Callayers workspace. Make sure Windows App Runtime 1.6 is installed, then reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
    catch(...) {Resources().event("workspace_runtime_failed");MessageBoxW(nullptr,L"Couldn't start the Callayers workspace. Close and reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
    MFShutdown();CoUninitialize();ReleaseMutex(single);CloseHandle(single);return result;
}
