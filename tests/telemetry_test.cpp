#include "telemetry.h"
#include "consumer_telemetry.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <condition_variable>
using namespace callayers;
using namespace winrt::Windows::Data::Json;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    winrt::init_apartment();std::vector<TelemetryEvent> events;
    TelemetryState state([&](auto e){events.push_back(std::move(e));});
    state.created("annotation");check(events.empty(),"signed-out action captured");
    state.begin();state.begin();check(events.size()==1,"duplicate session");
    const std::vector<TelemetryCard> card{{"saved_id","ai"}};
    state.observe({true,false,false,"none",0},card,{});auto count=events.size();
    state.observe({true,false,false,"none",0},card,{});check(events.size()==count,"repeated frames counted");
    state.observe({true,true,false,"brave",1},card,card);check(events.size()==count+1,"connected neutral video counted as output");
    state.observe({true,true,true,"brave",1},card,card);check(events.back().name=="card_usage_changed" && std::get<std::string>(events.back().properties.at("destination"))=="output","output card missing");
    state.observe({false,true,true,"brave",1},{},card);check(events.back().name=="card_usage_changed" && !std::get<bool>(events.back().properties.at("applied")) && std::get<std::string>(events.back().properties.at("destination"))=="preview","minimize ended output instead of preview");
    count=events.size();state.observe({false,true,true,"brave",2},{},card);check(events.size()==count+4,"quick reconnect lost boundaries");
    state.observe({false,true,true,"brave",2},{},{});check(!std::get<bool>(events.back().properties.at("applied")),"hidden card not removed");
    state.created("ai_revision","saved_id");check(std::get<std::string>(events.back().properties.at("kind"))=="ai_revision","revision not counted");
    state.end();check(events.back().name=="video_state_changed" && std::get<bool>(events.back().properties.at("consumer_connected")) && !std::get<bool>(events.back().properties.at("output_active")),"app exit invented a consumer disconnect");
    count=events.size();state.end();state.created("annotation");check(events.size()==count,"events emitted after sign-out");state.begin();check(events.back().name=="session_started","new account session missing");
    check(NormalizeConsumer(L"C:\\private\\BRAVE.EXE")==ConsumerApp::Brave,"browser normalization");
    check(NormalizeConsumer(L"FrameServer.exe")==ConsumerApp::Unknown,"broker incorrectly attributed");
    auto name=L"Local\\Callayers.TelemetryTest."+std::to_wstring(GetCurrentProcessId());ConsumerTelemetry writer(name.c_str()),reader(name.c_str());
    writer.started(ConsumerApp::Zoom);auto s=reader.snapshot(20);check(s.connected && !s.output && s.app==ConsumerApp::Zoom,"connect metadata");
    writer.published(20,123,7);check(reader.publishedScene(20,123)==7 && reader.publishedScene(20,124)==0,"scene metadata crossed frames");
    writer.delivered(true,20,7);s=reader.snapshot(20);check(s.output && s.scene==7,"fresh successful delivery missing");
    writer.delivered(true,20,7,S_FALSE);check(!reader.snapshot(20).output,"rejected sample counted as output");writer.delivered(true,20,7);
    check(!reader.snapshot(21).output,"old session still active");check(!reader.snapshot(20,GetTickCount64()+1001).connected,"stale consumer still connected");
    writer.delivered(false,20,7);check(!reader.snapshot(20).output,"neutral delivery counted");writer.stopped();check(!reader.snapshot(20).connected,"disconnect missing");
    std::mutex mutex;std::condition_variable changed;std::vector<std::string> bodies;std::vector<std::wstring> owners;
    auto diagnostic=std::filesystem::current_path()/L"diagnostics"/L"telemetry-test.jsonl";
    {
        TelemetrySender sender([&](const TelemetryCredentials& c,const std::string& body){std::lock_guard lock(mutex);bodies.push_back(body);owners.push_back(c.uid);changed.notify_all();return bodies.size()==1?503:200;},diagnostic);
        sender.record({"session_started",{}});sender.account({L"https://synthetic.invalid",L"owner_A",L"SYNTHETIC_TOKEN"},true);sender.record({"session_started",{}});
        std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return bodies.size()>=1;}),"first delivery did not occur");
        lock.unlock();sender.account({L"https://synthetic.invalid",L"owner_B",L"OTHER_SYNTHETIC_TOKEN"},true);sender.record({"session_started",{}});lock.lock();
        check(changed.wait_for(lock,std::chrono::seconds(8),[&]{return bodies.size()>=3;}),"bounded retry and second account delivery did not occur");
        check(bodies[0]==bodies[1] && owners[0]==L"owner_A" && owners[1]==L"owner_A","retry changed original event or owner");
        check(owners[2]==L"owner_B","account switch attributed a new event to the previous owner");
        auto body=JsonObject::Parse(winrt::to_hstring(bodies[0]));auto item=body.GetNamedArray(L"events").GetObjectAt(0);check(body.GetNamedArray(L"events").Size()==1,"unsigned action was queued");
        check(!item.HasKey(L"uid") && !item.HasKey(L"token") && item.GetNamedObject(L"properties").Size()==0,"private payload");
        check(item.GetNamedString(L"id").size()==36 && item.GetNamedString(L"sessionId").size()==36,"stable event/session IDs missing");
        check(JsonObject::Parse(winrt::to_hstring(bodies[2])).GetNamedArray(L"events").GetObjectAt(0).GetNamedString(L"sessionId")!=item.GetNamedString(L"sessionId"),"account switch reused a session ID");
    }
    std::atomic<size_t> total{0};{
        TelemetrySender bounded([&](const auto&,const std::string& body){auto items=JsonObject::Parse(winrt::to_hstring(body)).GetNamedArray(L"events");check(items.Size()<=32,"oversized batch");total+=items.Size();return 200;},diagnostic);
        bounded.account({L"https://synthetic.invalid",L"owner",L"SYNTHETIC_TOKEN"},true);for(int i=0;i<1000;++i)bounded.record({"content_created",{{"kind",std::string("annotation")}}});
    }check(total>0 && total<=1000,"bounded shutdown delivery failed");
    std::cout<<"Telemetry state, consumer attribution, freshness, privacy and retry checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}catch(...){std::cerr<<"Telemetry check failed\n";return 1;}}
