#include "text_renderer.h"
#include "sample_cards.h"
#include <iostream>
using namespace callayers;
void Expect(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try{
        Scene scene;TextElement text;text.id=NewId();text.text=L"Hello \"Callayers\"\nUnicode: 日本語 😀";text.bold=true;text.color=0xccaaff;scene.texts.push_back(text);
        Expect(DecodeScene(EncodeScene(scene))==scene,"JSON round trip lost content or style");
        auto file=std::filesystem::current_path()/(L"callayers-editor-"+NewId()+L".json");
        SaveScene(file,scene);Expect(LoadScene(file)==scene,"disk round trip failed");
        auto revised=scene;revised.texts[0].text=L"Updated";SaveScene(file,revised);Expect(LoadScene(file)==revised,"atomic replacement failed");std::filesystem::remove(file);
        Document document;document.Loaded(scene);auto before=document.scene;
        for(int i=0;i<20;++i)document.scene.texts[0]=MoveText(text,i*.005,i*.003);
        document.Commit(before);Expect(document.CanUndo()&&document.Dirty(),"drag was not recorded");document.Undo();
        Expect(document.scene==scene&&!document.CanUndo()&&!document.Dirty(),"drag did not coalesce to one undo");document.Redo();Expect(document.scene!=scene,"redo failed");
        auto side=ResizeText(text,5,.15,0);Expect(side.size==text.size&&side.height==text.height&&side.width>text.width,"side resize changed font size");
        auto corner=ResizeText(text,2,.1,.05);Expect(corner.size>text.size&&std::abs(corner.width/text.width-corner.size/text.size)<1e-9,"corner resize failed to scale text");
        auto invalid=EncodeScene(scene);
        auto root=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(invalid));root.SetNamedValue(L"version",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(99));
        bool rejected=false;try{DecodeScene(winrt::to_string(root.Stringify()));}catch(...){rejected=true;}Expect(rejected,"future file version accepted");
        auto duplicate=scene;duplicate.texts.push_back(text);rejected=false;try{DecodeScene(EncodeScene(duplicate));}catch(...){rejected=true;}Expect(rejected,"duplicate identities accepted");
        auto mixed=scene;mixed.cards={SampleCard(),SampleCard(true)};
        Expect(DecodeScene(EncodeScene(mixed))==mixed,"card deck round trip lost HTML or geometry");
        auto moved=MoveCard(mixed.cards[0],.9,.9);Expect(moved.x+moved.width<=1&&moved.y+moved.height<=1,"card escaped stage");
        auto resized=ResizeCard(mixed.cards[0],2,.12,-.05);Expect(resized.html==mixed.cards[0].html&&resized.width>mixed.cards[0].width&&resized.height<mixed.cards[0].height,"card resize lost content or independent dimensions");
        auto duplicated=mixed;duplicated.cards[0].id=text.id;rejected=false;try{DecodeScene(EncodeScene(duplicated));}catch(...){rejected=true;}Expect(rejected,"cross-layer duplicate identity accepted");
        TextRenderer renderer;
        auto fitted=text;fitted.text=L"One line";renderer.FitHeight(fitted);double oneLine=fitted.height;
        fitted.text=L"One line\nSecond line\nThird line";renderer.FitHeight(fitted);Expect(fitted.height>oneLine*2,"multiline text did not grow its box");
        fitted.text=L"One line";renderer.FitHeight(fitted);Expect(fitted.height==oneLine,"deleting lines did not shrink the box");
        fitted.size*=2;renderer.FitHeight(fitted);Expect(fitted.height>oneLine*1.8,"font size did not resize the box");
        fitted=text;fitted.text=L"A heading that wraps when the box gets narrower";fitted.width=.8;renderer.FitHeight(fitted);auto wide=fitted.height;
        fitted.width=.15;renderer.FitHeight(fitted);Expect(fitted.height>wide,"wrapping did not grow the box");
        auto layer=renderer.Render(scene);Expect(!layer->luma.empty(),"text renderer produced no pixels");
        auto background=Pattern(0,true);auto original=background->pixels;auto output=ComposeText(*background,*layer);
        Expect(output->pixels!=original,"text did not reach output frame");Expect(background->pixels==original,"composition mutated the capture frame");
        auto empty=renderer.Render({});Expect(ComposeText(*background,*empty)->pixels==original,"empty scene altered output");
        Expect(output->pixels[0]==original[0],"text escaped its bounds");
        std::cout<<"PASS: Unicode JSON and disk save/load, atomic replacement, drag undo/redo, resize semantics, invalid files, text composition\n";
    }catch(const winrt::hresult_error& error){std::wcerr<<error.message().c_str()<<L"\n";return 1;}
    catch(HRESULT hr){std::wcerr<<ErrorText(hr)<<L"\n";return 1;}
    catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
    return 0;
}
