#include "ai_client.h"
#include <iostream>
#include <thread>
using namespace callayers;
int wmain(int argc,wchar_t** argv){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try{
        if(argc==3&&std::wstring_view(argv[1])==L"--fetch"){
            std::atomic<bool> canceled=false;auto source=FetchWebsite(argv[2],canceled);
            std::wcout<<L"Fetched "<<source.url<<L": "<<source.text.size()<<L" characters; truncated="<<source.truncated<<L"\n"<<source.text.substr(0,1000)<<L"\n";return 0;
        }
        if(argc==2&&std::wstring_view(argv[1])==L"--key-status"){std::cout<<(HasAiKey()?"API key available\n":"API key not configured\n");return HasAiKey()?0:2;}
        if(argc<3){std::cout<<"AiProbe <prompt> <output.callayers.json> [existing.callayers.json]\n";return 2;}
        Document document;if(argc>3)document.Loaded(LoadScene(argv[3]));
        auto target=CaptureCardTarget(document,document.scene.cards.empty()?L"":document.scene.cards.front().id);
        AiJob job;auto started=Clock::now();job.Start(LoadAiSettings(),target,argv[1]);
        for(int i=0;i<1200;++i){
            if(auto result=job.TakeResult()){
                if(!result->card){std::wcerr<<result->error<<L"\n";return 1;}
                if(!ApplyGeneratedCard(document,target,result->card->html,result->card->name))throw std::runtime_error("Target changed");
                SaveScene(argv[2],document.scene);
                std::cout<<"PASS: generated and saved card in "<<std::chrono::duration<double>(Clock::now()-started).count()<<"s\n";return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        throw std::runtime_error("Generation did not finish within two minutes.");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}catch(...){std::cerr<<"Could not generate or save the card.\n";return 1;}
}
