#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <mferror.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include "capture.h"
#ifdef CALLAYERS_ULTRALIGHT_CPU
#include "overlay_ultralight.h"
#include "pen_preview.h"
#else
#include "overlay.h"
#endif
#include "frame_bridge.h"
#include "preview_frame.h"
#include "resource_diagnostics.h"
#include "workspace.h"
#include "camera_preferences.h"
#include "annotations.h"
#include "annotation_store.h"
#include "auth.h"
#ifdef CALLAYERS_INTERACTIVE
#include "card_input.h"
#endif
#ifdef CALLAYERS_GENERATION
#include "generation.h"
#ifdef CALLAYERS_IMAGES
#include "image_import.h"
#include <shobjidl.h>
#endif
#endif
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

struct App {
    HWND window = nullptr;
    std::unique_ptr<Workspace> workspace;
    HFONT font = nullptr, titleFont = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(29, 29, 33));
    HDC backDC = nullptr; HBITMAP backBitmap = nullptr; HGDIOBJ oldBitmap = nullptr;
    int backWidth = 0, backHeight = 0;
    FrameBridge bridge;
    Overlay overlay;
#ifdef CALLAYERS_ULTRALIGHT_CPU
    PenPreview penPreview;
#endif
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
    ULONGLONG pictureWaitSince=0;
    static constexpr ULONGLONG PictureWaitMs=15000;
    std::atomic<unsigned> commandVersion{1};
    std::atomic<bool> demand{false}, quitting{false};
    std::atomic<HRESULT> captureError{S_OK};
    std::atomic<bool> opening{false};
    std::thread worker;
    std::shared_ptr<const PresentedFrame> preview;
#ifdef CALLAYERS_ULTRALIGHT_CPU
    std::shared_ptr<const CameraImage> previewCamera;
    unsigned previewCameraVersion=0;
#endif
    void presentPreview(std::shared_ptr<PresentedFrame> frame) {
        std::lock_guard lock(previewMutex);
        if(ShouldReplacePreview(preview.get(),*frame))preview=std::move(frame);
    }
#ifdef CALLAYERS_ULTRALIGHT_CPU
    void refreshCardPreview() {
        if(!authenticated || !auth.outputAllowed() || !demand || quitting || FAILED(captureError.load()) || IsIconic(window))return;
        std::shared_ptr<const CameraImage> camera;unsigned version;
        {std::lock_guard lock(previewMutex);camera=previewCamera;version=previewCameraVersion;}
        if(!camera || version!=commandVersion || GetTickCount64()-camera->tick>=FreshMs)return;
        // Render the current geometry during this input event, then compose it
        // with the latest camera image instead of waiting for the video loop.
        overlay.capture();
        std::shared_ptr<OverlayImage> layer;uint64_t hiddenText=0;unsigned epoch;
        {std::lock_guard lock(overlay.state->mutex);
            epoch=overlay.state->epoch;if(epoch!=overlay.state->currentEpoch)return;
            layer=overlay.state->image;
            if(overlay.state->privateImage && overlay.state->privateTextId && overlay.state->privateTextId==overlay.state->requestedPrivateTextId){layer=overlay.state->privateImage;hiddenText=overlay.state->privateTextId;}
        }
        if(!layer || overlay.state->failed || GetTickCount64()-layer->tick>=FreshMs)return;
        auto frame=std::make_shared<PresentedFrame>();ComposeFrame(*camera,*layer,frame->pixels);
        frame->live=true;frame->version=version;frame->tick=std::min(camera->tick,layer->tick);frame->cameraTick=camera->tick;
        frame->sceneRevision=layer->sceneRevision;frame->hiddenTextId=hiddenText;
        if(version==commandVersion && epoch==overlay.state->currentEpoch && demand && auth.outputAllowed() && !quitting && SUCCEEDED(captureError.load()))presentPreview(std::move(frame));
    }
#endif
#ifdef CALLAYERS_INTERACTIVE
#ifdef CALLAYERS_GENERATION
#ifdef CALLAYERS_LIBRARY
    std::vector<Card> cards;
#else
    std::vector<Card> cards{{48,328,480,340},{870,62,340,148},{902,470,300,180}};
#endif
    Generation generation;
    void restoreGeneratedCards(){overlay.loadGenerated();for(auto& c:overlay.generated()){Card layout{c.x,c.y,c.w,c.h,c.visible};layout.contentW=c.width;layout.contentH=c.height;cards.push_back(layout);}syncCards();}
#ifdef CALLAYERS_LIBRARY
    int libraryIndex(const std::wstring& id)const{for(size_t i=0;i<overlay.generated().size();++i)if(overlay.generated()[i].id==id)return int(i);return -1;}
    void rebuildLibrary(){cards.clear();for(auto& c:overlay.generated()){Card layout{c.x,c.y,c.w,c.h,c.visible};layout.contentW=c.width;layout.contentH=c.height;cards.push_back(layout);}if(selectedCard>=cardCount())selectedCard=-1;}
    std::vector<int> hitOrder()const{std::vector<int> order;for(int i=0;i<cardCount();++i)order.push_back(i);std::sort(order.begin(),order.end(),[this](int a,int b){return overlay.generated()[a].layer>overlay.generated()[b].layer;});return order;}
#endif
    void syncGeneratedLayout(){for(int i=SavedCardOffset;i<cardCount();++i){auto& c=cards[i];overlay.generatedLayout(i-SavedCardOffset,c.x,c.y,c.w,c.h,c.visible);}}
    void refreshGeneration(){
        auto access=generation.takeAccessError();if(access==2){signOut();return;}if(access==1)auth.retry();
        auto result=generation.takeResult();if(result && authenticated && auth.outputAllowed()){
#ifdef CALLAYERS_LIBRARY
            if(!result->target.empty()){
                if(!overlay.reviseCard(result->target,result->contentVersion,result->card)){generation.message(L"Edit not applied. Your current card was kept.");MessageBoxW(window,L"This edit couldn't be applied. The card may have changed or been deleted. Your current work was kept.",L"Edit not applied",MB_OK|MB_ICONINFORMATION);}
            }else
#endif
            if(overlay.addGenerated(result->card)){auto& value=result->card;Card c{value.x,value.y,value.w,value.h,false};c.contentW=value.width;c.contentH=value.height;cards.push_back(c);selectedCard=cardCount()-1;changed();}
            else MessageBoxW(window,L"The card couldn't be added to your library. Your existing cards were preserved.",L"Card not saved",MB_OK|MB_ICONINFORMATION);
        }
#ifdef CALLAYERS_LIBRARY
        int applied=overlay.pollReplacement();if(applied>0){rebuildLibrary();changed();generation.message(L"Card saved.");}else if(applied<0)MessageBoxW(window,L"Couldn't save this edit. Your existing card was kept. Choose Retry.",L"Edit not saved",MB_OK|MB_ICONINFORMATION);
#endif
        if(workspace){auto snapshot=generation.snapshot();
#ifdef CALLAYERS_LIBRARY
            if(overlay.applying()){snapshot.busy=true;snapshot.applying=true;snapshot.message=FAILED(overlay.state->generatedSaveError.load())?L"Couldn't save this edit. Choose Retry.":L"Saving your edit…";}
#endif
            workspace->generation(snapshot);}
    }
#else
    Card cards[3]{{48, 328, 480, 340}, {870, 62, 340, 148}, {902, 470, 300, 180}};
#endif
    int focusedCard=-1,htmlDrag=-1;
    CardPart resizePart=CardPart::BottomRight;
    void blurCard(){overlay.blur();focusedCard=-1;}
    InkPoint inputPoint(int x,int y)const{return {double(x-previewRect.left)*Width/std::max(1L,previewRect.right-previewRect.left),double(y-previewRect.top)*Height/std::max(1L,previewRect.bottom-previewRect.top)};}
    void htmlUp(int x,int y){
        if(htmlDrag<0)return;auto p=inputPoint(x,y);overlay.mouse(2,int(p.x),int(p.y));overlay.pointerTarget(-1);htmlDrag=-1;
        if(GetCapture()==window)ReleaseCapture();
    }
#else
    Card cards[3]{{48, 540, 420, 128}, {870, 62, 340, 148}, {902, 470, 300, 180}};
#endif
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
    bool erasing=false,eraseChanged=false,eraseInside=false,eraserOnPreview=false;
    InkPoint erasePoint{},eraserCursor{};
    std::vector<Annotation> eraseOriginal;
    uint64_t eraseOriginalNextId=0;
    int selectedCard = -1;
    InkPoint dragStart{}; Card original{};
    RECT previewRect{};
    float dpi = 1;
    std::wstring status;
    bool authenticated=false,rendererInitialized=false;
    unsigned authRevision=unsigned(-1);
    Auth auth;
    int px(double value) const { return int(std::lround(value * dpi)); }
    int cardCount()const{return int(std::size(cards));}
    ~App() {
        if (oldBitmap) SelectObject(backDC, oldBitmap);
        if (backBitmap) DeleteObject(backBitmap);
        if (backDC) DeleteDC(backDC);
        if (font) DeleteObject(font); if (titleFont) DeleteObject(titleFont); DeleteObject(background);
    }
    void invalidateOutput() { bridge.stop(); }
    void lockVideo() {demand=false;++commandVersion;bridge.stop();}
    void refreshAuth() {
        auth.tick();auto login=auth.snapshot();
        auto url=auth.takeBrowserUrl();
        if(!url.empty() && reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32){auth.cancel();MessageBoxW(window,L"Couldn't open your browser. Check your Windows default browser, then try signing in again.",L"Google sign-in",MB_OK|MB_ICONINFORMATION);login=auth.snapshot();}
        if(authRevision==login.revision && authenticated==login.allowed)return;
        bool was=authenticated;authenticated=login.allowed;authRevision=login.revision;
        if(!authenticated){
#ifdef CALLAYERS_GENERATION
            if(generation.snapshot().busy)generation.cancel();
#endif
#ifdef CALLAYERS_INTERACTIVE
            blurCard();
#endif
            lockVideo();overlay.enable(false);
            {std::lock_guard lock(previewMutex);preview.reset();
#ifdef CALLAYERS_ULTRALIGHT_CPU
            previewCamera.reset();previewCameraVersion=0;
#endif
            }
            if(workspace && workspace->editingText())workspace->finishText(false);
            finishGesture(true);
        }else if(!rendererInitialized){rendererInitialized=true;overlay.initialize(GetModuleHandleW(nullptr));}
        if(workspace)workspace->authentication(login);
        if(was!=authenticated){if(authenticated)enumerate();layout();updateDemand();if(authenticated){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}}
    }
    void signOut() {
        lockVideo();
        if(workspace && !workspace->finishText())workspace->finishText(false);
        finishGesture();auth.signOut();refreshAuth();
    }
    std::wstring scene() const {
        std::wostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(2) << L"{\"cards\":[";
        for (int i = 0; i < cardCount(); ++i) {
            if (i) out << ',';
            const auto& c = cards[i];
#ifdef CALLAYERS_LIBRARY
            int renderId=overlay.generated()[i].renderId,layer=overlay.generated()[i].layer;
#else
            int renderId=i,layer=i;
#endif
            out << L"{\"layer\":"<<layer<<L",\"id\":" << renderId << L",\"x\":" << c.x << L",\"y\":" << c.y << L",\"w\":" << c.w << L",\"h\":" << c.h << L",\"contentW\":" << c.contentW << L",\"contentH\":" << c.contentH << L",\"visible\":" << (c.visible ? L"true" : L"false") << '}';
        }
        out << L"],\"annotations\":" << AnnotationJson(annotations) << '}'; return out.str();
    }
    void syncCards() { if(workspace) {
#ifdef CALLAYERS_LIBRARY
        workspace->libraryCards(overlay.generated(),selectedCard);
#else
        std::unique_ptr<bool[]> visible(new bool[cardCount()]);for(int i=0;i<cardCount();++i)visible[i]=cards[i].visible;
#ifdef CALLAYERS_GENERATION
        std::vector<std::pair<std::wstring,bool>> rows;for(int i=SavedCardOffset;i<cardCount();++i)rows.emplace_back(overlay.generated()[i-SavedCardOffset].name,cards[i].visible);workspace->generatedCards(rows);
#endif
        workspace->card(selectedCard,visible.get());
#endif
    } }
    void changed(bool syncSidebar=true) {
#ifdef CALLAYERS_GENERATION
        if(syncSidebar)syncGeneratedLayout();
#endif
        overlay.update(scene());
#ifdef CALLAYERS_ULTRALIGHT_CPU
        const bool livePen=drawing && selectedAnnotation>=0 && annotations[selectedAnnotation].kind==AnnotationTool::Pen;
        if(livePen)penPreview.follow(annotations[selectedAnnotation].id,overlay.sceneRevision());
#endif
        if(syncSidebar)syncCards();
        InvalidateRect(window,syncSidebar?nullptr:&previewRect,FALSE);
#ifdef CALLAYERS_ULTRALIGHT_CPU
        const bool movingCard=dragging && selectedCard>=0;
        if(movingCard)refreshCardPreview();
        // Paint the input result now instead of waiting behind more mouse moves.
        if(livePen || movingCard)UpdateWindow(window);
#endif
    }
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
        pictureWaitSince=0;
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
        bool wanted = authenticated && auth.outputAllowed() && selectedCamera >= 0 && (!IsIconic(window) || bridge.consumerActive());
        bool was = demand.exchange(wanted);
        if(was!=wanted)pictureWaitSince=0;
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
        annotations.erase(annotations.begin()+index);selectedAnnotation=hoveredAnnotation=-1;
        if(erasing)eraseChanged=true;
        changed(!erasing);syncAnnotationControls();if(!erasing)saveAnnotations();
    }
    void erasePen(InkPoint start,InkPoint end) {
        auto result=ErasePenPath(annotations,start,end,hitTolerance(),nextAnnotationId);
        if(result==PenEraseResult::Changed){eraseChanged=true;selectedAnnotation=hoveredAnnotation=-1;changed(false);}
        else if(result==PenEraseResult::LimitReached){finishGesture();MessageBoxW(window,L"There are too many stroke fragments. Remove an annotation before erasing more sections.",L"Annotations",MB_OK|MB_ICONINFORMATION);}
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
        if(!commit){editingAnnotationId=0;overlay.privatePreview(0);return true;}
        auto found=std::find_if(annotations.begin(),annotations.end(),[&](const auto& a){return a.id==editingAnnotationId;});
        bool empty=value.empty() || std::all_of(value.begin(),value.end(),[](wchar_t c){return std::iswspace(c)!=0;});
        if(empty){overlay.privatePreview(0);if(found!=annotations.end())eraseAnnotation(int(found-annotations.begin()));editingAnnotationId=0;return true;}
        Annotation a=editingTextDraft;
        if(found==annotations.end()){if(annotations.size()>=MaxAnnotations){MessageBoxW(window,L"There are too many annotations. Remove a few to continue.",L"Annotations",MB_OK);return false;}a.id=nextAnnotationId;}
        if(!std::isfinite(height) || height<=0 || height>Height-a.box.y){MessageBoxW(window,L"This text runs past the bottom of the picture. Shorten it before finishing.",L"Text doesn't fit",MB_OK|MB_ICONINFORMATION);return false;}
        a.text=std::move(value);a.box.h=height;
        if(found!=annotations.end()){selectedAnnotation=int(found-annotations.begin());*found=std::move(a);}
        else {++nextAnnotationId;annotations.push_back(std::move(a));selectedAnnotation=int(annotations.size())-1;}
        editingAnnotationId=0;overlay.privatePreview(0);changed();syncAnnotationControls();saveAnnotations();return true;
    }
    void beginText(InkPoint p,int existing=-1) {
        if(workspace && !workspace->finishText())return;
        finishGesture();
        editingAnnotationId=existing>=0?annotations[existing].id:0;
        overlay.privatePreview(editingAnnotationId);
        if(existing>=0)editingTextDraft=annotations[existing];
        else {editingTextDraft=Annotation{};editingTextDraft.kind=AnnotationTool::Text;editingTextDraft.color=inkColor;editingTextDraft.size=inkSize;editingTextDraft.font=TextSizes[inkSize];editingTextDraft.box={std::clamp(p.x,0.0,Width-480.0),std::clamp(p.y,0.0,Height-editingTextDraft.font*1.2),480,editingTextDraft.font*1.2};}
        selectedAnnotation=existing;
        selectedCard=-1;syncCards();syncAnnotationControls();
        auto at=inkRect(editingTextDraft.box);double scale=double(previewRect.right-previewRect.left)/Width;
        at.right=at.left+int(std::ceil(editingTextDraft.box.w*scale));at.bottom=previewRect.bottom;
        workspace->editText(at,editingTextDraft.text,editingTextDraft.font,editingTextDraft.color,editingTextDraft.box.w,scale,editingTextDraft.box.h,existing>=0);
        InvalidateRect(window,nullptr,FALSE);
    }
    void setTool(AnnotationTool chosen) {
#ifdef CALLAYERS_INTERACTIVE
        blurCard();
#endif
        finishGesture();if(workspace && !workspace->finishText()){syncAnnotationControls();return;}
        tool=chosen;hoveredAnnotation=-1;eraserOnPreview=false;if(tool!=AnnotationTool::Select)selectedCard=-1;
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
#ifdef CALLAYERS_INTERACTIVE
        if(htmlDrag>=0){overlay.mouse(2,-1000,-1000);overlay.pointerTarget(-1);htmlDrag=-1;}
#endif
        bool edited=drawing || draggingAnnotation;
        const bool movedCard=dragging && selectedCard>=0;
        const bool erased=erasing && eraseChanged;
        if(erasing && cancel && erased){annotations=std::move(eraseOriginal);nextAnnotationId=eraseOriginalNextId;selectedAnnotation=hoveredAnnotation=-1;}
        erasing=eraseChanged=eraseInside=false;std::vector<Annotation>().swap(eraseOriginal);
#ifdef CALLAYERS_ULTRALIGHT_CPU
        const bool penGesture=drawing && selectedAnnotation>=0 && annotations[selectedAnnotation].kind==AnnotationTool::Pen;
        if(cancel && penGesture)penPreview.cancel(annotations[selectedAnnotation].id);
        else if(cancel && !authenticated)penPreview.clear();
#endif
        if(cancel && edited && selectedAnnotation>=0){if(drawing)annotations.erase(annotations.begin()+selectedAnnotation);else annotations[selectedAnnotation]=originalInk;selectedAnnotation=-1;}
        else if(drawing && selectedAnnotation>=0){const auto& a=annotations[selectedAnnotation];auto b=Bounds(a);if((a.kind==AnnotationTool::Rectangle || a.kind==AnnotationTool::Ellipse) && (b.w<3 || b.h<3)){annotations.erase(annotations.begin()+selectedAnnotation);selectedAnnotation=-1;}}
        drawing=dragging=draggingAnnotation=resizing=false;
#ifdef CALLAYERS_GENERATION
        syncGeneratedLayout();overlay.saveGenerated();
#endif
        if(edited || erased || movedCard){changed();syncAnnotationControls();if((edited || erased) && !cancel)saveAnnotations();}
#ifdef CALLAYERS_ULTRALIGHT_CPU
        if(movedCard){refreshCardPreview();UpdateWindow(window);}
#endif
        if(GetCapture()==window)ReleaseCapture();
#ifdef CALLAYERS_ULTRALIGHT_CPU
        if(penGesture)UpdateWindow(window);
#endif
    }
    void pointerDown(int x,int y) {
        if(workspace && !workspace->finishText())return;
        POINT at{x,y};if(!authenticated || !auth.outputAllowed() || !PtInRect(&previewRect,at) || !demand || FAILED(captureError.load())){
#ifdef CALLAYERS_INTERACTIVE
            blurCard();
#endif
            return;
        }
        SetFocus(window);InkPoint p=canvasPoint(x,y);dragStart=p;
        if(tool==AnnotationTool::Text){int hit=annotationAt(p);beginText(p,hit>=0 && annotations[hit].kind==AnnotationTool::Text?hit:-1);return;}
        if(tool==AnnotationTool::Eraser){
#ifdef CALLAYERS_INTERACTIVE
            blurCard();
#endif
            eraseOriginal=annotations;eraseOriginalNextId=nextAnnotationId;
            erasing=true;eraseChanged=false;eraseInside=eraserOnPreview=true;erasePoint=eraserCursor=p;
            selectedCard=-1;SetCapture(window);int hit=annotationAt(p);
            if(hit>=0 && annotations[hit].kind!=AnnotationTool::Pen)eraseAnnotation(hit);else erasePen(p,p);
            selectedAnnotation=hoveredAnnotation=-1;syncCards();syncAnnotationControls();InvalidateRect(window,&previewRect,FALSE);return;
        }
        if(tool!=AnnotationTool::Select) {
            size_t count=0;for(const auto& a:annotations)count+=a.points.size();
            if(annotations.size()>=MaxAnnotations || count>=MaxInkPoints-2){MessageBoxW(window,L"There are too many annotations. Remove a few to continue.",L"Annotations",MB_OK);return;}
            Annotation a;a.id=nextAnnotationId++;a.kind=tool;a.color=drawingColor();a.size=inkSize;a.box={p.x,p.y,0,0};
            if(tool==AnnotationTool::Pen || tool==AnnotationTool::Line || tool==AnnotationTool::Arrow)a.points={p,{std::min(double(Width),p.x+0.1),p.y}};
            annotations.push_back(std::move(a));selectedAnnotation=int(annotations.size())-1;selectedCard=-1;drawing=true;
        }else {
#ifdef CALLAYERS_INTERACTIVE
            if(selectedCard>=0 && cards[selectedCard].visible){const auto& c=cards[selectedCard];
                auto part=HitCardPart(c.x,c.y,c.w,c.h,p.x,p.y,hitTolerance());
                if(part!=CardPart::None && part!=CardPart::Inside){
                    original=c;dragging=true;resizing=part!=CardPart::Border;resizePart=part;
                    selectedAnnotation=-1;blurCard();SetCapture(window);changed();syncAnnotationControls();return;
                }
            }
#endif
            // Private handles are hit before the painted objects, especially an
            // arrow's endpoint and a stroke's empty lower-right bounding corner.
            bool handle=false;
            if(selectedAnnotation>=0){const auto& a=annotations[selectedAnnotation];auto b=Bounds(a);InkPoint end{b.x+b.w,b.y+b.h};if((a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow)&&a.points.size()==2)end=a.points[1];handle=std::hypot(p.x-end.x,p.y-end.y)<=hitTolerance()*1.8;}
            int hit=handle?selectedAnnotation:annotationAt(p);
            selectedAnnotation=hit;
            if(hit>=0){selectedCard=-1;originalInk=annotations[hit];draggingAnnotation=true;resizing=handle;}
            else {selectedCard=-1;
#ifdef CALLAYERS_LIBRARY
for(int i:hitOrder()){const auto& c=cards[i];
#else
for(int i=cardCount()-1;i>=0;--i){const auto& c=cards[i];
#endif
#ifdef CALLAYERS_INTERACTIVE
                if(!c.visible)continue;
                auto part=HitCardPart(c.x,c.y,c.w,c.h,p.x,p.y,hitTolerance());
                if(part==CardPart::None)continue;
                selectedCard=i;original=c;
                if(part==CardPart::Inside){
                    focusedCard=i;htmlDrag=i;overlay.pointerTarget(
#ifdef CALLAYERS_LIBRARY
overlay.generated()[i].renderId
#else
i
#endif
);overlay.mouse(1,int(p.x),int(p.y));SetCapture(window);
                    hoveredAnnotation=-1;changed();syncAnnotationControls();return;
                }
                blurCard();dragging=true;resizing=part!=CardPart::Border;resizePart=part;break;
#else
                if(c.visible && p.x>=c.x && p.x<=c.x+c.w && p.y>=c.y && p.y<=c.y+c.h){selectedCard=i;original=c;dragging=true;resizing=p.x>c.x+c.w-28 && p.y>c.y+c.h-28;break;}
#endif
            }}
        }
#ifdef CALLAYERS_INTERACTIVE
        blurCard();
#endif
        hoveredAnnotation=-1;SetCapture(window);changed();syncAnnotationControls();
    }
    void pointerMove(int x,int y) {
        InkPoint p=canvasPoint(x,y);
#ifdef CALLAYERS_INTERACTIVE
        if(htmlDrag>=0){auto point=inputPoint(x,y);overlay.mouse(0,int(point.x),int(point.y));return;}
        if(tool==AnnotationTool::Select && !dragging && !draggingAnnotation){POINT at{x,y};if(PtInRect(&previewRect,at))overlay.mouse(0,int(p.x),int(p.y));}
#endif
        if(tool==AnnotationTool::Eraser){
            POINT at{x,y};bool inside=PtInRect(&previewRect,at);
            if(erasing && GetCapture()==window){if(inside)erasePen(eraseInside?erasePoint:p,p);erasePoint=p;eraseInside=inside;}
            eraserOnPreview=inside;eraserCursor=p;hoveredAnnotation=inside?annotationAt(p):-1;
            InvalidateRect(window,&previewRect,FALSE);
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);return;
        }
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
            if(resizing){
#ifdef CALLAYERS_INTERACTIVE
                const bool left=resizePart==CardPart::TopLeft||resizePart==CardPart::BottomLeft;
                const bool top=resizePart==CardPart::TopLeft||resizePart==CardPart::TopRight;
                double dx=(p.x-dragStart.x)*(left?-1:1),dy=(p.y-dragStart.y)*(top?-1:1);
                double factor=1+(dx*original.w+dy*original.h)/(original.w*original.w+original.h*original.h);
                double maxW=left?original.x+original.w:Width-original.x,maxH=top?original.y+original.h:Height-original.y;
                double maximum=std::min(maxW/original.w,maxH/original.h),minimum=std::min(maximum,std::max(180.0/original.w,100.0/original.h));
                factor=std::clamp(factor,minimum,maximum);c.w=original.w*factor;c.h=original.h*factor;
                c.x=left?original.x+original.w-c.w:original.x;c.y=top?original.y+original.h-c.h:original.y;
#else
                double factor=1+((p.x-dragStart.x)*original.w+(p.y-dragStart.y)*original.h)/(original.w*original.w+original.h*original.h);double maximum=std::min((Width-c.x)/original.w,(Height-c.y)/original.h),minimum=std::min(maximum,std::max(180.0/original.w,100.0/original.h));factor=std::clamp(factor,minimum,maximum);c.w=original.w*factor;c.h=original.h*factor;
#endif
            }
            else {c.x=std::clamp(original.x+p.x-dragStart.x,0.0,Width-c.w);c.y=std::clamp(original.y+p.y-dragStart.y,0.0,Height-c.h);}
        }else return;
        changed(!((drawing && selectedAnnotation>=0 && annotations[selectedAnnotation].kind==AnnotationTool::Pen) || (dragging && selectedCard>=0)));
    }
    void doubleClick(int x,int y){POINT at{x,y};if(!PtInRect(&previewRect,at) || !demand || FAILED(captureError.load()))return;InkPoint p=canvasPoint(x,y);int hit=annotationAt(p);if((tool==AnnotationTool::Select || tool==AnnotationTool::Text)&&hit>=0&&annotations[hit].kind==AnnotationTool::Text){finishGesture();beginText(p,hit);}else pointerDown(x,y);}
    void text(HDC dc, const std::wstring& value, RECT rect, UINT flags = DT_LEFT | DT_WORDBREAK) { DrawTextW(dc, value.c_str(), -1, &rect, flags); }
    void paintAnnotationControls(HDC dc) {
        if(workspace && workspace->editingText())return;
        if(tool==AnnotationTool::Eraser && eraserOnPreview){
            double radius=hitTolerance();auto circle=inkRect({eraserCursor.x-radius,eraserCursor.y-radius,radius*2,radius*2});
            int saved=SaveDC(dc);IntersectClipRect(dc,previewRect.left,previewRect.top,previewRect.right,previewRect.bottom);
            HPEN pen=CreatePen(PS_SOLID,px(1),RGB(255,130,130));auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
            Ellipse(dc,circle.left,circle.top,circle.right,circle.bottom);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);RestoreDC(dc,saved);
        }
        int index=tool==AnnotationTool::Eraser?hoveredAnnotation:tool==AnnotationTool::Select?selectedAnnotation:-1;
        if(index<0 || index>=int(annotations.size()))return;
        const auto& a=annotations[index];if(tool==AnnotationTool::Eraser && a.kind==AnnotationTool::Pen)return;auto b=Bounds(a);RECT r=inkRect(b);
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
                HBRUSH b=CreateSolidBrush(RGB(109,211,255));
#ifdef CALLAYERS_INTERACTIVE
                for(auto x:{r.left,r.right})for(auto y:{r.top,r.bottom}){RECT handle{x-px(6),y-px(6),x+px(6),y+px(6)};FillRect(dc,&handle,b);}
#else
                RECT handle{r.right-px(12),r.bottom-px(12),r.right,r.bottom};FillRect(dc,&handle,b);
#endif
                DeleteObject(b);
            }
#ifdef CALLAYERS_ULTRALIGHT_CPU
            penPreview.paint(dc,annotations,previewRect,frame->sceneRevision);
#endif
            paintAnnotationControls(dc);
        }
        BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY); EndPaint(window,&ps);
        if(workspace)workspace->textBackdrop(fresh && (!editingAnnotationId || frame->hiddenTextId==editingAnnotationId));
    }
    void refreshWorkspace() {
        if(!authenticated){if(workspace)workspace->state(L"",L"",false,false,false,false);return;}
        std::shared_ptr<const PresentedFrame> frame;
        { std::lock_guard lock(previewMutex); frame=preview; }
        HRESULT error=captureError.load();
        bool fresh=frame && frame->live && frame->version==commandVersion && GetTickCount64()-frame->tick<FreshMs && demand && SUCCEEDED(error) && !overlay.state->failed;
        // Give an opening camera time to deliver a picture before offering
        // recovery. Reset after live video or a pause so a resume gets its own wait.
        if(fresh || !demand)pictureWaitSince=0;
        else if(!pictureWaitSince)pictureWaitSince=GetTickCount64();
        bool pictureTimedOut=pictureWaitSince && GetTickCount64()-pictureWaitSince>=PictureWaitMs;
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
        else if(!fresh && pictureTimedOut) {state=L"Preview unavailable";message=L"A picture hasn't arrived. Choose Retry, or choose another camera.";}
        else if(!fresh) {state=L"Opening your webcam";message=L"Opening your webcam…";}
        else state=bridge.consumerActive()?L"Video is reaching your calling app":L"Ready for your call";
        if(fresh && preferenceSaveFailed)state=L"Couldn't remember webcam";
        if(fresh && annotationSaveFailed)state=L"Couldn't save annotations";
#ifdef CALLAYERS_INTERACTIVE
        if(fresh && FAILED(overlay.state->saveError.load())){state=L"Couldn't save card state";message=L"Your card values couldn't be saved or restored. Your video can continue. Choose Retry. An unreadable saved file will not be overwritten.";}
#endif
        if(fresh && FAILED(annotationRestoreError))state=L"Couldn't restore annotations";
        auto login=auth.snapshot();if(fresh && !login.message.empty())state=login.message;
#ifdef CALLAYERS_GENERATION
        if(fresh && FAILED(overlay.state->generatedSaveError.load())){state=L"Card save unavailable";message=L"Saved cards or their values could not be saved or restored. Choose Retry. Unreadable saved data will be preserved.";}
#endif
        status=state;
        bool cameraChoices=!fresh && (selectedCamera<0 || overlay.state->failed || FAILED(error) || pictureTimedOut);
        bool cardSaveFailed=false;
#ifdef CALLAYERS_INTERACTIVE
        cardSaveFailed=FAILED(overlay.state->saveError.load());
#ifdef CALLAYERS_GENERATION
        cardSaveFailed|=FAILED(overlay.state->generatedSaveError.load());
#endif
#endif
        if(workspace)workspace->state(state,message,fresh,error==E_ACCESSDENIED,fresh && (preferenceSaveFailed || annotationSaveFailed || FAILED(annotationRestoreError) || cardSaveFailed || !login.message.empty()),cameraChoices);
    }
    void videoLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            Capture camera;
            unsigned version=0; bool started=false, attempted=false; LONG64 session=0;
            std::shared_ptr<OverlayImage> overlayFrame,privateOverlayFrame;
            uint64_t privateTextId=0;
            auto next=std::chrono::steady_clock::now();
            while(!quitting) {
                unsigned requested=commandVersion;
                if(requested!=version || !demand) {
                    if(started || attempted) { bridge.stop(); camera.stop(); }
                    started=false; attempted=false; overlayFrame.reset();privateOverlayFrame.reset(); version=requested;
                }
                if(demand && auth.outputAllowed() && !attempted) {
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
#ifdef CALLAYERS_ULTRALIGHT_CPU
                        {
                            std::lock_guard lock(overlay.state->mutex);
                            if(overlay.state->epoch==overlay.state->currentEpoch){
                                overlayFrame=overlay.state->image;
                                privateOverlayFrame=overlay.state->privateImage;
                                privateTextId=overlay.state->privateTextId;
                            }else{overlayFrame.reset();privateOverlayFrame.reset();privateTextId=0;}
                        }
#else
                        std::vector<uint8_t> png,privatePng; ULONGLONG tick=0; unsigned epoch=0;uint64_t textId=0;
                        {std::lock_guard lock(overlay.state->mutex);png.swap(overlay.state->png);privatePng.swap(overlay.state->privatePng);tick=overlay.state->tick;epoch=overlay.state->epoch;textId=overlay.state->privateTextId;}
                        if(!png.empty() && epoch==overlay.state->currentEpoch) {
                            auto decoded=DecodeOverlay(png,tick);
                            auto privateDecoded=privatePng.empty()?nullptr:DecodeOverlay(privatePng,tick);
                            if(decoded && epoch==overlay.state->currentEpoch){
                                overlayFrame=std::move(decoded);
                                if(privateDecoded){privateOverlayFrame=std::move(privateDecoded);privateTextId=textId;}
                                else if(!privateTextId || privateTextId!=overlay.state->requestedPrivateTextId){privateOverlayFrame.reset();privateTextId=0;}
                            }
                        }
#endif
                        std::shared_ptr<const CameraImage> image;
                        {std::lock_guard lock(camera.state->mutex);image=camera.state->latest;}
#ifdef CALLAYERS_ULTRALIGHT_CPU
                        if(version==commandVersion && demand){std::lock_guard lock(previewMutex);previewCamera=image;previewCameraVersion=version;}
#endif
                        ULONGLONG now=GetTickCount64();
                        bool fresh=image && overlayFrame && now-image->tick<FreshMs && now-overlayFrame->tick<FreshMs && !overlay.state->failed;
                        if(fresh && version==commandVersion && demand && auth.outputAllowed() && !quitting) {
                            auto result=std::make_shared<PresentedFrame>();
                            ComposeFrame(*image,*overlayFrame,result->pixels); result->live=true; result->version=version;
                            result->tick=std::min(image->tick,overlayFrame->tick);result->cameraTick=image->tick;
#ifdef CALLAYERS_ULTRALIGHT_CPU
                            result->sceneRevision=overlayFrame->sceneRevision;
#endif
                            if(version==commandVersion && demand && auth.outputAllowed() && !quitting) {
                                { StageTimer timing(VideoStage::Publish); bridge.publish(result->pixels,session,result->tick); }
                                // The completed overlay alone crosses the camera bridge. Only
                                // this window's preview hides the text being privately edited.
                                if(privateOverlayFrame && privateTextId && privateTextId==overlay.state->requestedPrivateTextId){ComposeFrame(*image,*privateOverlayFrame,result->pixels);result->hiddenTextId=privateTextId;}
                                presentPreview(std::move(result));
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
            actions.visibility=[app](int index,bool show){if(index<0||index>=app->cardCount())return;app->cards[index].visible=show;app->changed();
#ifdef CALLAYERS_GENERATION
                app->overlay.saveGenerated();
#endif
            };
#ifdef CALLAYERS_GENERATION
            actions.generate=[app](std::wstring prompt){if(app->authenticated && app->auth.outputAllowed())app->generation.start(prompt,app->auth.idToken());};
            actions.cancelGeneration=[app]{app->generation.cancel();};
#ifdef CALLAYERS_LIBRARY
            actions.renameCard=[app](std::wstring id,std::wstring name){
                auto first=name.find_first_not_of(L" \t\r\n"),last=name.find_last_not_of(L" \t\r\n");name=first==std::wstring::npos?L"":name.substr(first,last-first+1);
                if(!app->overlay.renameCard(app->libraryIndex(id),name))MessageBoxW(app->window,L"Use a name between 1 and 80 characters. If an edit is being saved, wait and try again.",L"Name not saved",MB_OK|MB_ICONINFORMATION);app->syncCards();
            };
            actions.editCard=[app](std::wstring id,std::wstring prompt){int i=app->libraryIndex(id);if(i<0 || app->overlay.applying())return;app->finishGesture();app->blurCard();app->overlay.saveGenerated();if(app->authenticated && app->auth.outputAllowed())app->generation.start(prompt,app->auth.idToken(),app->overlay.generated()[i]);};
            actions.deleteCard=[app](std::wstring id){int i=app->libraryIndex(id);if(i<0)return;app->finishGesture();app->blurCard();auto message=L"Delete \""+app->overlay.generated()[i].name+L"\"? It will be removed from your library and camera. This cannot be undone.";
                if(MessageBoxW(app->window,message.c_str(),L"Delete card",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return;
                if(app->overlay.deleteCard(i)){app->selectedCard=-1;app->rebuildLibrary();app->changed();}
            };
            actions.stackCard=[app](std::wstring id,bool front){app->finishGesture();if(app->overlay.stackCard(app->libraryIndex(id),front))app->changed();};
            actions.restoreCard=[app](std::wstring id){app->finishGesture();app->blurCard();if(app->overlay.restorePrevious(app->libraryIndex(id)))app->refreshGeneration();};
#endif
#ifdef CALLAYERS_IMAGES
            actions.importImage=[app]{
                if(!app->authenticated || !app->auth.outputAllowed() || !app->workspace->finishText())return;
                app->finishGesture();app->blurCard();
                if(app->overlay.generated().size()>=16){MessageBoxW(app->window,L"This build supports up to 16 saved cards.",L"Library full",MB_OK|MB_ICONINFORMATION);return;}
                try{
                    winrt::com_ptr<IFileOpenDialog> dialog;winrt::check_hresult(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put())));
                    COMDLG_FILTERSPEC filter{L"Images (PNG, JPEG, BMP)",L"*.png;*.jpg;*.jpeg;*.bmp"};winrt::check_hresult(dialog->SetFileTypes(1,&filter));
                    winrt::check_hresult(dialog->SetOptions(FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM));dialog->SetTitle(L"Import image");
                    auto result=dialog->Show(app->window);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;winrt::check_hresult(result);
                    winrt::com_ptr<IShellItem> item;winrt::check_hresult(dialog->GetResult(item.put()));
                    PWSTR raw=nullptr;winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH,&raw));std::filesystem::path path(raw);CoTaskMemFree(raw);
                    auto image=ImportImage(path);auto layout=Card{image.x,image.y,image.w,image.h,false};layout.contentW=image.width;layout.contentH=image.height;
                    if(!app->overlay.addGenerated(std::move(image)))throw std::runtime_error("The image couldn't be saved. Choose Retry and try again.");
                    app->cards.push_back(layout);app->selectedAnnotation=-1;app->selectedCard=app->cardCount()-1;app->setTool(AnnotationTool::Select);app->changed();
                }catch(const std::runtime_error& e){MessageBoxW(app->window,winrt::to_hstring(e.what()).c_str(),L"Couldn't import image",MB_OK|MB_ICONINFORMATION);}
                catch(...){MessageBoxW(app->window,L"Couldn't read this image. Choose a working PNG, JPEG or BMP and try again.",L"Couldn't import image",MB_OK|MB_ICONINFORMATION);}
            };
#endif
#endif
            actions.tool=[app](AnnotationTool tool){app->setTool(tool);};
            actions.color=[app](std::wstring color){app->setInkColor(std::move(color));};
            actions.size=[app](int size){app->setInkSize(size);};
            actions.text=[app](std::wstring text,bool commit,double height){return app->commitText(std::move(text),commit,height);};
            actions.retry=[app]{
#ifdef CALLAYERS_GENERATION
                if(app->status==L"Card save unavailable"){app->overlay.retryState();app->selectedCard=-1;app->blurCard();app->cards.resize(SavedCardOffset);app->restoreGeneratedCards();app->changed();return;}
#endif
#ifdef CALLAYERS_INTERACTIVE
                if(app->status==L"Couldn't save card state"){app->overlay.retryState();return;}
#endif
                auto login=app->auth.snapshot();if(login.allowed && !login.message.empty()){app->auth.retry();return;}
                if(app->status==L"Couldn't restore annotations"){if(app->restoreAnnotations(!app->annotations.empty())){app->changed();app->syncAnnotationControls();app->saveAnnotations();}return;}
                if(app->status==L"Couldn't save annotations"){app->saveAnnotations();return;}
                if(app->preferenceSaveFailed && SUCCEEDED(app->captureError.load()) && app->demand && !app->overlay.state->failed) {
                    app->preferenceAttemptedVersion=0;app->preferenceSaveFailed=false;app->refreshWorkspace();return;
                }
                if(app->overlay.state->failed)app->overlay.retry(GetModuleHandleW(nullptr));app->enumerate();
            };
            actions.privacy=[] {ShellExecuteW(nullptr,L"open",L"ms-settings:privacy-webcam",nullptr,nullptr,SW_SHOWNORMAL);};
            actions.hideAll=[app]{for(auto& card:app->cards)card.visible=false;app->changed();
#ifdef CALLAYERS_GENERATION
                app->overlay.saveGenerated();
#endif
            };
            actions.clear=[app]{if(!app->workspace->finishText())return;app->finishGesture();if(MessageBoxW(app->window,L"Clear all annotations? This cannot be undone.",L"Clear annotations",MB_YESNO|MB_ICONQUESTION)==IDYES){app->annotations.clear();app->selectedAnnotation=app->hoveredAnnotation=-1;app->changed();app->syncAnnotationControls();app->saveAnnotations();}};
            actions.quit=[app]{PostMessageW(app->window,WM_CLOSE,0,0);};
            actions.authPrimary=[app]{auto login=app->auth.snapshot();if(login.retryAvailable)app->auth.retry();else app->auth.signIn();app->refreshAuth();};
            actions.authCancel=[app]{app->auth.cancel();app->refreshAuth();};
            actions.signOut=[app]{app->signOut();};
            actions.layout=[app]{app->layout();};
            app->restoreAnnotations();
#ifdef CALLAYERS_GENERATION
            app->restoreGeneratedCards();
#endif
            app->workspace=std::make_unique<Workspace>(window,std::move(actions));
            app->auth.onRevoke([app]{app->lockVideo();});app->bridge.stop();app->workspace->authentication(app->auth.snapshot());
            app->dpi=GetDpiForWindow(window)/96.0f;app->buildFonts();app->enumerate(true);app->refreshWorkspace();app->layout();app->syncCards();app->syncAnnotationControls();
            app->overlay.update(app->scene());app->auth.restore();app->refreshAuth();
            app->worker=std::thread([app]{app->videoLoop();});SetTimer(window,1,33,nullptr);return 0;
        } catch(winrt::hresult_error const& error) {Resources().event("workspace_controls_failed hresult="+std::to_string(error.code().value));MessageBoxW(window,L"Couldn't open the workspace. Close and reopen Callayers.",L"Callayers",MB_OK|MB_ICONERROR);return -1;}
        catch(...) {Resources().event("workspace_controls_failed");MessageBoxW(window,L"Couldn't open the workspace. Close and reopen Callayers.",L"Callayers",MB_OK|MB_ICONERROR);return -1;}
    }
    case WM_TIMER:app->refreshAuth();app->updateDemand();app->overlay.capture();
#ifdef CALLAYERS_GENERATION
        app->refreshGeneration();
#endif
        app->refreshWorkspace();if(!IsIconic(window))InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_SIZE:if(app->workspace){app->layout();app->updateDemand();}return 0;
    case WM_DPICHANGED: {
        app->dpi=HIWORD(wParam)/96.0f;app->buildFonts();auto r=reinterpret_cast<RECT*>(lParam);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->layout();return 0;
    }
    case WM_GETMINMAXINFO: {auto limits=reinterpret_cast<MINMAXINFO*>(lParam);limits->ptMinTrackSize={app->px(1000),app->px(760)};return 0;}
    case WM_KEYDOWN:
#ifdef CALLAYERS_INTERACTIVE
        if(app->focusedCard>=0){if(wParam==VK_ESCAPE){app->blurCard();return 0;}app->overlay.key(message,wParam,lParam);return 0;}
#endif
        if(wParam==VK_TAB && app->workspace){app->workspace->focus((GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
        if(wParam==VK_ESCAPE){app->finishGesture(true);return 0;}
        if(wParam==VK_DELETE && app->authenticated && app->auth.outputAllowed()){app->eraseAnnotation(app->selectedAnnotation);return 0;}break;
    case WM_LBUTTONDOWN:app->pointerDown(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_MOUSEMOVE:app->pointerMove(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_LBUTTONUP:
#ifdef CALLAYERS_INTERACTIVE
        if(app->htmlDrag>=0){app->htmlUp(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;}
#endif
        if((app->drawing && app->tool==AnnotationTool::Pen) || app->erasing || app->dragging)app->pointerMove(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
        app->finishGesture();return 0;
#ifdef CALLAYERS_INTERACTIVE
    case WM_CHAR:case WM_KEYUP:if(app->focusedCard>=0){app->overlay.key(message,wParam,lParam);return 0;}break;
    case WM_KILLFOCUS:app->blurCard();app->finishGesture();return 0;
    case WM_MOUSEWHEEL:{POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(window,&point);
        if(app->tool==AnnotationTool::Select && PtInRect(&app->previewRect,point)){auto p=app->canvasPoint(point.x,point.y);app->overlay.scroll(int(p.x),int(p.y),GET_WHEEL_DELTA_WPARAM(wParam));return 0;}break;}
#endif
    case WM_CAPTURECHANGED:app->finishGesture();return 0;
    case WM_LBUTTONDBLCLK:app->doubleClick(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_MOUSELEAVE:app->hoveredAnnotation=-1;app->eraserOnPreview=app->eraseInside=false;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_SETCURSOR:if(reinterpret_cast<HWND>(wParam)==window && LOWORD(lParam)==HTCLIENT){POINT at{};GetCursorPos(&at);ScreenToClient(window,&at);if(PtInRect(&app->previewRect,at)){SetCursor(LoadCursorW(nullptr,app->tool==AnnotationTool::Text?IDC_IBEAM:app->tool==AnnotationTool::Select?IDC_ARROW:IDC_CROSS));return TRUE;}}break;
    case WM_ERASEBKGND:return 1;
    case WM_PRINTCLIENT:if(app->backDC){RECT client{};GetClientRect(window,&client);BitBlt(reinterpret_cast<HDC>(wParam),0,0,client.right,client.bottom,app->backDC,0,0,SRCCOPY);}return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:SetTextColor(reinterpret_cast<HDC>(wParam),RGB(230,235,245));SetBkColor(reinterpret_cast<HDC>(wParam),RGB(18,22,31));return reinterpret_cast<LRESULT>(app->background);
    case WM_PAINT:app->paint();return 0;
    case WM_CLOSE:
        if(app->workspace && !app->workspace->finishText())return 0;
        app->finishGesture();
        app->auth.cancel();
        if(app->bridge.consumerActive() && MessageBoxW(window,L"Quit Callayers and stop video in your calling app?",L"Stop video and quit",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
#ifdef CALLAYERS_INTERACTIVE
        app->overlay.saveCard();
#endif
#ifdef CALLAYERS_GENERATION
        app->generation.cancel();app->overlay.saveGenerated();
#endif
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
            if(window){ShowWindow(window,show);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){
#ifdef CALLAYERS_INTERACTIVE
                const bool cardKey=app.focusedCard>=0 && message.hwnd==window && message.message>=WM_KEYFIRST && message.message<=WM_KEYLAST;
                if(cardKey || !ContentPreTranslateMessage(&message))
#else
                if(!ContentPreTranslateMessage(&message))
#endif
                {TranslateMessage(&message);DispatchMessageW(&message);}
            }}
            else {app.quitting=true;result=1;}
            if(app.worker.joinable())app.worker.join();
        }
    }
    catch(winrt::hresult_error const& error) {Resources().event("workspace_runtime_failed hresult="+std::to_string(error.code().value));MessageBoxW(nullptr,L"Couldn't start the Callayers workspace. Make sure Windows App Runtime 1.6 is installed, then reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
    catch(...) {Resources().event("workspace_runtime_failed");MessageBoxW(nullptr,L"Couldn't start the Callayers workspace. Close and reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
    MFShutdown();CoUninitialize();ReleaseMutex(single);CloseHandle(single);return result;
}
