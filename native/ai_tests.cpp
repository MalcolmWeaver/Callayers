#include "ai_client.h"
#include <iostream>
using namespace callayers;
void Expect(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try{
        Document document;CardElement card;card.id=NewId();card.html=L"<h1>Original</h1>";document.scene.cards.push_back(card);document.Saved();
        auto target=CaptureCardTarget(document,card.id);auto before=document.scene;document.scene.cards[0].x=.2;document.Commit(before);
        Expect(ApplyGeneratedCard(document,target,L"<h1>Revised</h1>",L"Revision"),"movement invalidated request");
        Expect(document.scene.cards[0].x==.2&&document.scene.cards[0].id==card.id,"revision changed placement or identity");
        document.Undo();Expect(document.scene.cards[0].html==card.html&&document.scene.cards[0].x==.2,"revision not one undo");
        Expect(!ApplyGeneratedCard(document,target,L"<h1>Stale</h1>",L"Stale"),"undo revived stale request");
        target=CaptureCardTarget(document,card.id);before=document.scene;document.scene.cards.clear();document.Commit(before);document.Undo();
        Expect(!CardTargetCurrent(document,target),"delete/undo revived a stale target");
        target=CaptureCardTarget(document,card.id);document.Loaded(document.scene);Expect(!CardTargetCurrent(document,target),"reopened document accepted old request");
        auto create=CaptureCardTarget(document);Expect(ApplyGeneratedCard(document,create,L"<h1>New</h1>",L"New"),"new card failed");
        Expect(document.scene.cards.size()==2,"creation replaced an existing card");document.Undo();Expect(document.scene.cards.size()==1,"creation undo failed");
        auto request=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(BuildCardRequest({},CaptureCardTarget(document,card.id),L"Keep the layout; make it blue")));
        Expect(!request.GetNamedBoolean(L"store"),"request storage enabled");Expect(std::wstring(request.GetNamedString(L"input")).find(L"Original")!=std::wstring::npos,"revision omitted original HTML");
        auto response=R"({"status":"completed","output":[{"type":"reasoning"},{"type":"message","content":[{"type":"output_text","text":"{\"name\":\"Test\",\"html\":\"<html><body>Hello</body></html>\"}"}]}]})";
        auto parsed=ParseCardResponse(response);Expect(parsed.name==L"Test","structured response did not parse");
        Expect(WebsiteUrl(L"Summarize https://streamfog.com.")==L"https://streamfog.com","URL punctuation parsing");
        Expect(WebsiteUrl(L"Make a card with no website").empty(),"unrequested retrieval");
        for(auto invalid:{L"http://example.com",L"https://localhost",L"https://127.0.0.1",L"https://user:pass@example.com",L"https://example.com https://example.org"}){
            bool rejected=false;try{WebsiteUrl(invalid);}catch(...){rejected=true;}Expect(rejected,"unsafe or multiple URLs accepted");}
        auto extracted=WebsiteText(L"<html><head><style>badstyle</style><script>badscript</script></head><body><h1>Hello &amp; world</h1><p>Readable text.</p><!--badcomment--><script src='test'>badscript2</script></body></html>");
        Expect(extracted.find(L"Hello & world")!=std::wstring::npos&&extracted.find(L"Readable text.")!=std::wstring::npos&&extracted.find(L"bad")==std::wstring::npos,"website extraction includes executable content or loses text");
        WebsiteSource source{L"https://example.com",L"Actual retrieved facts",true};
        auto grounded=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(BuildCardRequest({},target,L"Summarize the website",source)));
        auto groundedInput=winrt::Windows::Data::Json::JsonObject::Parse(grounded.GetNamedString(L"input"));
        Expect(groundedInput.GetNamedObject(L"website_source").GetNamedString(L"text")==source.text,"source missing from model input");
        Expect(groundedInput.GetNamedObject(L"website_source").GetNamedBoolean(L"truncated"),"truncation not disclosed to model");
        for(auto invalid:{R"({"status":"incomplete","output":[]})",R"({"status":"completed","output":[{"type":"message","content":[{"type":"refusal"}]}]})",R"({"status":"completed","output":[]})"}){
            bool rejected=false;try{ParseCardResponse(invalid);}catch(...){rejected=true;}Expect(rejected,"incomplete/refused/malformed response accepted");}
        Expect(DecodeScene(EncodeScene(document.scene))==document.scene,"card names did not survive save/reopen");
        std::cout<<"PASS: target identity/revision, move during generation, stale/delete/undo/open protection, atomic history, request and response contract\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}catch(...){std::cerr<<"AI contract test failed\n";return 1;}
}
