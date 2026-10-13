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
int main(int argc,char** argv){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    HWND window=nullptr;
    try{
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"CallayersEditorTest";
        RegisterClassW(&wc);window=CreateWindowW(wc.lpszClassName,L"Editor test",WS_OVERLAPPEDWINDOW,0,0,1100,800,nullptr,nullptr,wc.hInstance,nullptr);
        Expect(window!=nullptr,"test window failed");
        std::shared_ptr<const TextOverlay> output;size_t published=0;
        auto controller=std::make_unique<TextEditorUI>(window,[&](auto layer){output=layer;++published;},argc==1);editor=controller.get();
        std::vector<AnnotationChange> annotationEvents;
        editor->SetAnnotationObserver([&](const AnnotationChange& change){annotationEvents.push_back(change);});
        editor->SetStage({20,165,980,705});
        SendMessageW(window,WM_COMMAND,TextEditorUI::TextId,0);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(120,245));
        SetWindowTextW(GetDlgItem(window,TextEditorUI::EditId),L"  \r\n");
        SendMessageW(window,WM_APP+20,1,0);
        Expect(editor->CurrentScene().texts.empty()&&annotationEvents.empty(),"abandoned empty text counted as an annotation");
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
        Expect(annotationEvents.empty(),"typing emitted annotation events before session end");
        SendMessageW(window,WM_APP+20,1,0);
        Expect(annotationEvents.size()==1&&std::wstring(annotationEvents[0].action)==L"created","text session did not emit exactly one creation");
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
        const auto beforeCancel=annotationEvents.size();
        SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Canceled change"));SendMessageW(window,WM_APP+20,0,0);Expect(ComposeText(*background,*output)->pixels==typing,"cancel did not restore original text");
        Expect(annotationEvents.size()==beforeCancel,"canceled text session emitted telemetry");
        SendMessageW(window,WM_LBUTTONDBLCLK,0,MAKELPARAM(150,270));SendMessageW(window,WM_APP+20,1,0);
        Expect(annotationEvents.size()==beforeCancel,"unchanged text session emitted telemetry");
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(150,270));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(150,270));
        SendMessageW(window,WM_COMMAND,TextEditorUI::DeleteId,0);Expect(output->luma.empty(),"delete failed");
        SendMessageW(window,WM_COMMAND,TextEditorUI::UndoId,0);Expect(ComposeText(*background,*output)->pixels==typing,"undo delete failed");
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(150,270));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(150,270));
        editor->ChangeStyle([](auto& t){t.family=L"Georgia";t.size=84;t.bold=true;t.alignment=1;});
        Expect(editor->Selection()->family==L"Georgia"&&editor->Selection()->size==84,"shell style command did not reach selected text");
        editor->Command(TextEditorUI::UndoId);Expect(ComposeText(*background,*output)->pixels==typing,"shell style command did not undo in one step");
        if(argc>1&&std::string(argv[1])=="--annotation-session"){
            editor=nullptr;controller.reset();DestroyWindow(window);
            std::cout<<"PASS: text sessions, empty creation, typing, cancellation, undo, and formatting\n";return 0;
        }
        Scene cards;editor->SceneChanged=[&](const Scene& next){cards=next;};
        editor->AddSampleCard();Expect(editor->CardSelected()&&editor->CurrentScene().cards.size()==1&&cards.cards.empty(),"new card must enter the library without going on camera");
        editor->SetCardShown(0,true);Expect(cards.cards.size()==1,"checked card did not publish");
        auto originalCard=cards.cards[0];
        int moveX=20+int(std::lround((originalCard.x+originalCard.width/2)*960));
        int moveY=165+int(std::lround(originalCard.y*540))-int(14*GetDpiForWindow(window)/96.);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(moveX,moveY));
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(moveX+50,moveY+25));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(moveX+50,moveY+25));Expect(cards.cards[0].x>originalCard.x,"card move handle did not move card");
        editor->Command(TextEditorUI::UndoId);Expect(cards.cards[0]==originalCard,"card drag was not one undo");
        editor->SelectCard(0);editor->Command(TextEditorUI::DeleteId);Expect(cards.cards.empty()&&editor->CurrentScene().cards.empty(),"card delete failed");
        editor->Command(TextEditorUI::UndoId);Expect(editor->CurrentScene().cards.size()==1&&editor->CurrentScene().cards[0]==originalCard&&cards.cards.empty(),"card undo must restore the library without showing the card");
        editor->SetCardShown(0,true);editor->SelectCard(0);
        auto request=editor->GenerationTarget();Expect(!request.creating,"prompt lost selected card");
        editor->AddSampleCard();auto secondId=editor->CurrentScene().cards.back().id;
        Expect(editor->AcceptGenerated(request,L"<h1>AI revision</h1>",L"Revised first card"),"late result missed original target");
        Expect(editor->CurrentScene().cards[0].name==L"Revised first card"&&editor->CurrentScene().cards[1].id==secondId&&editor->CardName()!=L"Revised first card","generation changed current selection or wrong card");
        editor->NewCardSelection();request=editor->GenerationTarget();Expect(request.creating,"explicit new card retained edit target");
        Expect(editor->AcceptGenerated(request,L"<h1>Brand new</h1>",L"New AI card")&&editor->CardName()==L"New AI card","new card not created and selected");
        auto beforeDrawing=editor->CurrentScene();auto beforePixels=ComposeText(*background,*output)->pixels;
        editor->SetDrawingTool(TextEditorUI::DrawTool::Pen);editor->SetPenColor(0xffdc64);editor->SetPenWidth(12);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,600));
        for(int x=120;x<=500;x+=20)SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,600));
        editor->TickInk();Expect(ComposeText(*background,*output)->pixels!=beforePixels,"live ink did not reach output");
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(520,600));
        auto drawn=editor->CurrentScene();Expect(drawn.ink.size()==1&&drawn.ink[0].color==0xffdc64&&drawn.ink[0].width==12,"pen style or stroke lost");
        Expect(drawn.cards==beforeDrawing.cards&&drawn.texts==beforeDrawing.texts,"drawing changed presentation objects");
        editor->Command(TextEditorUI::UndoId);Expect(editor->CurrentScene()==beforeDrawing,"stroke was not one undo");
        editor->Command(TextEditorUI::RedoId);Expect(editor->CurrentScene()==drawn,"stroke redo lost points");
        editor->SetDrawingTool(TextEditorUI::DrawTool::Eraser);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(300,550));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(300,650));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(300,650));
        Expect(editor->CurrentScene()==beforeDrawing,"eraser missed crossing or changed unrelated objects");
        editor->Command(TextEditorUI::UndoId);Expect(editor->CurrentScene()==drawn,"eraser gesture did not undo");
        auto savedDrawing=EncodeScene(drawn);auto drawingPixels=ComposeText(*background,*output)->pixels;
        editor->SetDrawingTool(TextEditorUI::DrawTool::Laser);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(700,620));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(800,640));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(800,640));
        Expect(ComposeText(*background,*output)->pixels!=drawingPixels,"laser absent from output");
        Expect(EncodeScene(editor->CurrentScene())==savedDrawing,"laser leaked into saved scene");
        editor->TickInk(Clock::now()+std::chrono::seconds(2));Expect(ComposeText(*background,*output)->pixels==drawingPixels,"laser did not fully fade");
        editor->Command(TextEditorUI::UndoId);Expect(editor->CurrentScene()==beforeDrawing,"laser polluted undo history");
        editor->SetDrawingTool(TextEditorUI::DrawTool::Pen);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(650,550));SendMessageW(window,WM_CAPTURECHANGED,0,0);
        Expect(editor->CurrentScene().ink.size()==1,"capture loss lost a pen dot");editor->Command(TextEditorUI::UndoId);Expect(editor->CurrentScene()==beforeDrawing,"capture-loss stroke did not undo");
        editor=nullptr;controller.reset();DestroyWindow(window);
        std::cout<<"PASS: text tool, live typing, edit commit/cancel, select without output change, drag coalescing, delete/undo\n";
    }catch(const winrt::hresult_error& error){std::wcerr<<error.message().c_str()<<L"\n";return 1;}
    catch(HRESULT hr){std::wcerr<<ErrorText(hr)<<L"\n";return 1;}
    catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
    return 0;
}
