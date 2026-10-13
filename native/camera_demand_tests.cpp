#include "camera_demand.h"
#include <iostream>
using namespace callayers;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void Eventually(F test,const char* message){
    auto end=Clock::now()+std::chrono::seconds(3);
    while(Clock::now()<end){if(test())return;Sleep(10);}
    throw std::runtime_error(message);
}
int wmain(int argc,wchar_t** argv){
    if(argc==3&&std::wstring(argv[1])==L"--client"){
        CameraDemandClient client;
        for(;;){client.Update(true,argv[2]);Sleep(20);}
    }
    try{
        const auto name=std::wstring(DemandPipeName)+L".test."+std::to_wstring(GetCurrentProcessId());
        CameraDemandClient first,second;
        first.Update(true,name.c_str()); // Runtime may start after the consumer.
        auto server=std::make_unique<CameraDemandServer>(name.c_str());
        Require(server->Poll()==0,"idle runtime has demand");
        Eventually([&]{first.Update(true,name.c_str());return server->Poll()==1;},"first stream did not connect");
        Eventually([&]{second.Update(true,name.c_str());return server->Poll()==2;},"second stream did not connect");
        first.Update(false,name.c_str());
        Eventually([&]{return server->Poll()==1;},"closing one stream lost remaining consumer");
        Require(NeedsCameraFrames(false,server->Poll()),"hidden preview lost active stream demand");
        second.Update(false,name.c_str());
        Eventually([&]{return server->Poll()==0;},"last stream did not release demand");
        Require(!NeedsCameraFrames(false,0),"idle camera should be released");
        Require(NeedsCameraFrames(false,0,500,1499),"legacy reader lost capture when preview hid");
        Require(!NeedsCameraFrames(false,0,500,1500),"legacy reader did not expire after disconnect");
        Require(NeedsCameraFrames(false,1,500,2500),"persistent stream was expired by legacy timeout");
        Require(NeedsCameraFrames(true,0),"visible preview must keep capture");
        Require(NeedsCameraFrames(true,2),"preview and streams must keep capture");
        Require(!ShouldProduceFrames(true,true,false,0,0,1500),"empty setup started frame production");
        Require(ShouldProduceFrames(true,true,true,0,0,1500),"selected source did not start visible preview");
        Require(!ShouldProduceFrames(true,false,true,0,0,1500),"hidden source kept idle capture running");
        Require(ShouldProduceFrames(true,false,false,1,0,1500),"meeting app lost hidden output");
        Require(ShouldProduceFrames(true,false,false,0,500,1499),"legacy output request was ignored");
        Require(!ShouldProduceFrames(true,false,false,0,500,1500),"legacy output demand did not expire");
        Require(!ShouldProduceFrames(false,true,true,1,500,600),"locked account kept frames running");
        {
            CameraDemandClient abandoned;
            Eventually([&]{abandoned.Update(true,name.c_str());return server->Poll()==1;},"abandoned stream did not connect");
        }
        Eventually([&]{return server->Poll()==0;},"handle destruction leaked demand");
        Eventually([&]{first.Update(true,name.c_str());return server->Poll()==1;},"stream restart failed");
        server.reset();
        first.Update(true,name.c_str());
        server=std::make_unique<CameraDemandServer>(name.c_str());
        Eventually([&]{first.Update(true,name.c_str());return server->Poll()==1;},"runtime restart did not reconnect stream");
        first.Update(false,name.c_str());
        Eventually([&]{return server->Poll()==0;},"restart stream did not release");
        wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
        std::wstring command=L"\""+std::wstring(executable)+L"\" --client \""+name+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"could not launch crash-test client");
        Handle child(process.hProcess),thread(process.hThread);
        try{Eventually([&]{return server->Poll()==1;},"child stream did not connect");}
        catch(...){TerminateProcess(child.value,1);WaitForSingleObject(child.value,3000);throw;}
        Require(TerminateProcess(child.value,1)!=FALSE,"could not stop crash-test client");
        Require(WaitForSingleObject(child.value,3000)==WAIT_OBJECT_0,"crash-test child did not exit");
        Eventually([&]{return server->Poll()==0;},"crashed consumer leaked demand");
        std::cout<<"PASS: preview demand, multiple streams, disconnect, abandoned handle, runtime restart\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
    catch(HRESULT error){std::wcerr<<ErrorText(error)<<L"\n";return 1;}
}

