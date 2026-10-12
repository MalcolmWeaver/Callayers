#include "editor.h"
#include <iostream>
using namespace callayers;
void Expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try{
        Document document;std::vector<AnnotationChange> events;
        document.AnnotationsChanged=[&](const AnnotationChange& change){events.push_back(change);};
        auto check=[&](const wchar_t* action,const wchar_t* type,const wchar_t* source,size_t count){
            Expect(events.size()==1,"expected one coalesced event");const auto& e=events.front();
            Expect(std::wstring(e.action)==action&&std::wstring(e.type)==type&&std::wstring(e.source)==source&&e.count==count,"wrong action metadata");events.clear();
        };
        TextElement text;text.id=L"text-1";auto before=document.scene;document.scene.texts.push_back(text);
        for(int i=0;i<30;++i)document.scene.texts[0].text+=L"private";
        Expect(events.empty(),"keystrokes emitted before commit");document.Commit(before);check(L"created",L"text",L"direct",1);
        before=document.scene;for(int i=0;i<50;++i)document.scene.texts[0].x+=.001;
        document.Commit(before);check(L"edited",L"text",L"direct",1);
        document.Undo();check(L"edited",L"text",L"undo",1);document.Redo();check(L"edited",L"text",L"redo",1);
        document.Commit(document.scene);Expect(events.empty(),"no-op emitted");
        // Even unrelated document commits during an open text editor must not
        // fragment its annotation telemetry into individual changes.
        document.BeginTextAnnotationSession(document.scene);
        for(int i=0;i<20;++i){before=document.scene;document.scene.texts[0].text+=L"x";document.Commit(before);}
        Expect(events.empty(),"open text session emitted intermediate edits");
        document.EndTextAnnotationSession(true);check(L"edited",L"text",L"direct",1);
        document.EndTextAnnotationSession(true);Expect(events.empty(),"session emitted twice");
        document.BeginTextAnnotationSession(document.scene);document.EndTextAnnotationSession(true);
        Expect(events.empty(),"unchanged text session emitted");
        before=document.scene;document.BeginTextAnnotationSession(before);document.scene.texts[0].text=L"canceled session";
        document.scene=before;document.EndTextAnnotationSession(false);Expect(events.empty(),"canceled text session emitted");
        before=document.scene;document.scene.texts[0].text=L"canceled";document.scene=before;document.Commit(before);Expect(events.empty(),"canceled edit emitted");
        before=document.scene;document.scene.texts.clear();document.Commit(before);check(L"deleted",L"text",L"direct",1);
        document.Undo();check(L"created",L"text",L"undo",1);
        InkStroke ink;ink.id=L"ink-1";before=document.scene;document.scene.ink.push_back(ink);
        for(int i=0;i<100;++i)document.scene.ink[0].points.push_back({.1,.2});
        Expect(events.empty(),"pointer movement emitted before commit");document.Commit(before);check(L"created",L"drawing",L"direct",1);
        before=document.scene;auto ink2=ink;ink2.id=L"ink-2";document.scene.ink.push_back(ink2);document.Commit(before);events.clear();
        before=document.scene;document.scene.ink.clear();document.Commit(before);check(L"deleted",L"drawing",L"direct",2);
        document.Undo();check(L"created",L"drawing",L"undo",2);
        before=document.scene;document.scene.ink.clear();document.scene.texts.clear();document.Commit(before,true);
        Expect(events.size()==2&&std::wstring(events[0].action)==L"cleared"&&events[0].count==1&&std::wstring(events[1].action)==L"cleared"&&events[1].count==2,"clear was not grouped by type");events.clear();
        document.Undo();Expect(events.size()==2&&std::wstring(events[0].source)==L"undo","clear undo missing");events.clear();
        document.ClearAnnotations();Expect(events.size()==2&&std::wstring(events[0].source)==L"card_switch","automatic clear missing");events.clear();
        document.ClearAnnotations();Expect(events.empty(),"empty clear emitted");
        Scene loaded;loaded.texts.push_back(text);document.Loaded(loaded);Expect(events.empty(),"load falsely counted as creation");
        before=document.scene;document.scene.title=L"new title";CardElement card;card.id=L"card-1";document.scene.cards.push_back(card);document.Commit(before);Expect(events.empty(),"unrelated card change emitted");
        document.AnnotationsChanged=[](const AnnotationChange&){throw std::runtime_error("observer unavailable");};
        before=document.scene;document.scene.texts.clear();document.Commit(before);Expect(document.CanUndo(),"observer failure broke history");
        std::cout<<"PASS: annotation commits, coalescing, cancellation, undo/redo, clear, load, and observer isolation\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
