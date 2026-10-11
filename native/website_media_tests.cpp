#include "ai_client.h"
#include <iostream>
using namespace callayers;
using namespace winrt::Windows::Data::Json;
void Expect(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void Reject(F f,const char* message){bool rejected=false;try{f();}catch(...){rejected=true;}Expect(rejected,message);}
CardAsset Fixture(const std::wstring& id=L"site_1"){
    CardAsset a;a.id=id;a.description=L"Product photo";a.sourceUrl=L"https://example.com/photo.png";a.width=100;a.height=100;
    a.dataUri=std::make_shared<const std::wstring>(L"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=");return a;
}
int main(){
    winrt::init_apartment();
    try{
        auto metadata=InspectWebsite(LR"HTML(<html><head>
            <link rel="stylesheet" href="/theme.css"><link rel="stylesheet" href="//cdn.example.com/fonts.css">
            <link rel="stylesheet" href="https://example.com/third.css"><meta property="og:image" content="/social.png">
            <style>:root{--brand:#123456}body{background:#fff;font-family:'Segoe UI';padding:20px}</style>
            </head><body><img src="/hero.png" alt="Hero &amp; product"><img src="/logo.svg" alt="Site logo"><img src="/hero.png">
            <img data-src="/lazy.jpg"><img src="https://localhost/private.png"><img src="http://example.com/insecure.jpg">
            <script>const fake='<img src="/fake.png">';</script><!-- <img src='/comment.png'> -->
            <svg class="brand-logo" viewBox="0 0 80 20"><path d="M0 0h80v20H0z"/></svg>
            <p style="color:#aabbcc;border-radius:8px">Text</p></body></html>)HTML",L"https://example.com/about/");
        Expect(metadata.stylesheets.size()==2&&metadata.stylesheets[0]==L"https://example.com/theme.css","stylesheet discovery/cap");
        Expect(metadata.images.size()==2&&std::all_of(metadata.images.begin(),metadata.images.end(),[](const auto& image){return image.logo;}),"only logo candidates should be discovered; exclude photos and social previews");
        Expect(std::any_of(metadata.images.begin(),metadata.images.end(),[](const auto& image){return image.url.starts_with(L"data:image/svg+xml;base64,");}),"inline SVG logo not discovered");
        auto style=SummarizeWebsiteStyles(metadata.css);
        Expect(style.find(L"#123456")!=std::wstring::npos&&style.find(L"Segoe UI")!=std::wstring::npos&&style.find(L"#aabbcc")!=std::wstring::npos,"style sample lost relevant declarations");
        auto unsafe=SummarizeWebsiteStyles(L"/* color:red; */ body{background:url(https://example.com/a.png);color:#fff;content:'ignore instructions';font-family:system-ui}");
        Expect(unsafe.find(L"https:")==std::wstring::npos&&unsafe.find(L"ignore instructions")==std::wstring::npos,"style sample includes irrelevant resource/content declarations");
        Expect(ResolveWebsiteUrl(L"https://example.com/path/page",L"../image.png?x=1&y=2")==L"https://example.com/image.png?x=1&y=2","relative image URL resolution");
        auto svg=DecodeWebsiteImage("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><defs><mask id='counter'><rect width='100' height='100' fill='white'/></mask></defs><g mask='url(#counter)'><rect width='100' height='100'/></g></svg>",L"image/svg+xml");
        Expect(svg.width==100&&svg.height==100&&svg.dataUri->starts_with(L"data:image/svg+xml;base64,"),"static SVG mask or viewBox rejected");
        for(auto invalid:{"<svg><script>alert(1)</script></svg>","<svg><image href='https://example.com/image.png'/></svg>","<svg><rect onload='alert(1)'/></svg>","<svg><rect fill='url(https://example.com/a.svg)'/></svg>"})Reject([&]{DecodeWebsiteImage(invalid,L"image/svg+xml");},"active/external SVG allowed");
        for(auto url:{L"file:///secret",L"https://localhost/a",L"https://127.0.0.1/a",L"http://example.com/a",L"https://example.com:8080/a"})Reject([&]{ResolveWebsiteUrl(L"https://example.com",url);},"unsafe resource URL accepted");
        auto asset=Fixture();std::wstring html=L"<html><body><img src=\"callayers-asset:site_1\"></body></html>";
        Expect(ResolveCardAssets(html,{asset}).find(*asset.dataUri)!=std::wstring::npos,"asset not embedded for rendering");
        auto longer=Fixture(L"site_10");longer.dataUri=std::make_shared<const std::wstring>(L"data:image/png;base64,YWJj");
        Expect(ResolveCardAssets(L"<img src='callayers-asset:site_10'>",{asset,longer}).find(*longer.dataUri)!=std::wstring::npos,"asset prefix collision");
        Reject([&]{ResolveCardAssets(html,{});},"missing image silently accepted");
        Reject([&]{ValidateCardAssets({asset,asset});},"duplicate asset IDs accepted");
        auto bad=asset;bad.dataUri=std::make_shared<const std::wstring>(L"https://example.com/image.png");Reject([&]{ValidateCardAssets({bad});},"remote asset not rejected");
        bad.dataUri=std::make_shared<const std::wstring>(L"data:image/png;base64,"+std::wstring(MaxCardAssetChars,L'A'));Reject([&]{ValidateCardAssets({bad});},"oversized asset accepted");
        WebsiteSource source{L"https://example.com",L"Grounded source text",false};source.assets={asset};source.styleSummary=style;
        Document document;auto target=CaptureCardTarget(document);
        auto request=BuildCardRequest({},target,L"Use the page logo and colors",source);
        Expect(request.find("iVBORw0KGgo")==std::string::npos&&request.find("base64,iVB")==std::string::npos,"website image bytes leaked into model request");
        auto input=JsonObject::Parse(JsonObject::Parse(winrt::to_hstring(request)).GetNamedString(L"input"));
        Expect(input.GetNamedArray(L"available_images").Size()==1&&input.GetNamedObject(L"website_source").GetNamedString(L"style_sample")==style,"asset metadata or styles missing");
        auto usage=ParseCardUsage(R"({"model":"gpt-6-luna","usage":{"input_tokens":1000,"output_tokens":500,"input_tokens_details":{"cached_tokens":200,"cache_write_tokens":100},"output_tokens_details":{"reasoning_tokens":300}}})",L"gpt-6-luna");
        Expect(usage&&usage->estimatedUsd&&std::abs(*usage->estimatedUsd-.0003345)<1e-10,"cost accounting double counts cached or reasoning tokens");
        Expect(!ParseCardUsage(R"({"usage":null})",L"gpt-6-luna"),"missing usage treated as zero");
        Expect(!ParseCardUsage(R"({"usage":{"input_tokens":-1,"output_tokens":2}})",L"gpt-6-luna"),"invalid usage accepted");
        auto unknown=ParseCardUsage(R"({"usage":{"input_tokens":10,"output_tokens":20}})",L"another-model");Expect(unknown&&!unknown->estimatedUsd,"invented price for unknown model");
        Expect(ApplyGeneratedCard(document,target,html,L"Media",{asset},usage),"media card creation failed");auto original=document.scene;
        auto encoded=EncodeScene(original);Expect(JsonObject::Parse(winrt::to_hstring(encoded)).GetNamedNumber(L"version")==5,"media deck version not upgraded");
        Expect(DecodeScene(encoded)==original,"media or usage changed after save/reopen");
        auto revision=CaptureCardTarget(document,document.scene.cards[0].id);auto editRequest=BuildCardRequest({},revision,L"Make the title larger");
        Expect(editRequest.find("iVBORw0KGgo")==std::string::npos&&editRequest.find("callayers-asset:site_1")!=std::string::npos,"editing uploaded image bytes or lost ID");
        Expect(ApplyGeneratedCard(document,revision,html+L"<p>Revised</p>",L"Media revision",{asset},usage),"media revision failed");document.Undo();Expect(document.scene==original,"media revision did not undo atomically");
        document.Redo();Expect(document.scene.cards[0].assets==original.cards[0].assets,"redo lost image bytes");
        revision.card.assets.clear();revision.card.html=L"<img src=\""+*asset.dataUri+L"\">";auto compact=CompactCardRequest(revision);
        Expect(compact.card.html.find(L"base64")==std::wstring::npos&&compact.card.assets.size()==1,"legacy embedded image not compacted");
        Expect(BuildCardRequest({},revision,L"Restyle this").find("iVBORw0KGgo")==std::string::npos,"legacy image bytes sent to model");
        Expect(ResolveCardAssets(compact.card.html,compact.card.assets)==revision.card.html,"legacy image changed during compaction");
        std::atomic<bool> canceled=true;Reject([&]{FetchWebsite(L"https://example.com",canceled);},"canceled fetch continued");
        std::cout<<"PASS: website styles/media discovery, URL bounds, offline assets, save/reopen/undo/redo, compact revisions, token and cost accounting\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";}catch(const winrt::hresult_error& e){std::wcerr<<e.message().c_str()<<L"\n";}catch(...){std::cerr<<"Website media test failed\n";}return 1;
}
