#include "setup_flow.h"
#include "text_editor_ui.h"
#include <iostream>
using namespace callayers;
void Expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try{
        auto tag=NameTagCard(L"Alex <script>&\"",L"Design & research");
        Expect(tag.html.find(L"<script>")==std::wstring::npos&&tag.html.find(L"Alex &lt;script&gt;&amp;&quot;")!=std::wstring::npos,"name tag did not escape user text");
        Scene scene;scene.cards={tag};Expect(DecodeScene(EncodeScene(scene))==scene,"template card did not survive deck round trip");
        Expect(!LiveDeliveryRecent(10000,0)&&!LiveDeliveryRecent(10000,8000)&&LiveDeliveryRecent(10000,9999)&&!LiveDeliveryRecent(1,2),"connection accepted absent, expired, or future delivery");
        HWND window=CreateWindowW(L"STATIC",L"Setup test",WS_OVERLAPPEDWINDOW,0,0,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Expect(window!=nullptr,"test window failed");
        {
            TextEditorUI editor(window,[](auto){},false);Scene output;
            editor.SceneChanged=[&](const Scene& next){output=next;};
            editor.AddSetupCard(tag);
            Expect(editor.IsCardShown(tag.id)&&output.cards.size()==1&&output.cards[0].id==tag.id,"template did not appear immediately on camera");
            auto updated=NameTagCard(L"New name",L"New detail");editor.UpdateSetupCard(tag.id,updated.html);
            Expect(output.cards.size()==1&&output.cards[0].html==updated.html&&output.cards[0].id==tag.id,"editing template duplicated card or did not update output");
            editor.Command(TextEditorUI::UndoId);Expect(output.cards[0].html==tag.html,"template edit could not be undone");
            editor.Command(TextEditorUI::UndoId);Expect(editor.CurrentScene().cards.empty()&&output.cards.empty(),"undo creation left template on camera");
        }
        DestroyWindow(window);std::cout<<"PASS: setup template, immediate camera output, live edits, undo, escaping, persistence, delivery expiry\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
    catch(...){std::cerr<<"Setup test failed\n";return 1;}
}
