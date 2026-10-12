#pragma once
#include "setup_flow.h"
#include "text_editor_ui.h"
#include <shellapi.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace callayers {
namespace setupui=winrt::Microsoft::UI::Xaml;
namespace setupcontrols=setupui::Controls;

// A sidebar, not a modal: the real camera preview stays visible during every step.
class SetupPanel {
    HWND window;
    TextEditorUI& editor;
    std::function<void(int)> selectCamera;
    std::function<void()> retryOutput,closed;
    setupcontrols::StackPanel body;
    setupcontrols::TextBlock progress,title,description,status,guide,error;
    setupcontrols::ComboBox cameras,apps;
    setupcontrols::TextBox name,detail;
    setupcontrols::Button next,back,launch,skip,privacy,retry;
    setupcontrols::StackPanel cameraStep,cardStep,connectStep;
    int step=0;
    bool active=false,fresh=false,running=false,delivering=false,verified=false,permissionsOpened=false,syncingCamera=false;
    HRESULT cameraFailure=S_OK;
    std::wstring cardId;
    static void Text(setupcontrols::TextBlock label,const wchar_t* text){label.Text(text);label.TextWrapping(setupui::TextWrapping::Wrap);}
    static void Primary(setupcontrols::Button button){
        button.Background(setupui::Media::SolidColorBrush(winrt::Windows::UI::Color{255,163,155,250}));
        button.Foreground(setupui::Media::SolidColorBrush(winrt::Windows::UI::Color{255,32,29,51}));
        button.BorderThickness({0,0,0,0});button.MinHeight(42);
    }
    static void Quiet(setupcontrols::Button button){
        button.Background(setupui::Media::SolidColorBrush(winrt::Windows::UI::Color{0,0,0,0}));
        button.BorderThickness({0,0,0,0});button.Foreground(setupui::Media::SolidColorBrush(winrt::Windows::UI::Color{255,190,186,204}));
    }
    void EditTag(){
        if(CardIndex()>=0)editor.UpdateSetupCard(cardId,NameTagCard(std::wstring(name.Text()),std::wstring(detail.Text())).html);
        Refresh();
    }
    template<class F> void Button(setupcontrols::Button button,const wchar_t* label,F action){
        button.Content(winrt::box_value(label));button.HorizontalAlignment(setupui::HorizontalAlignment::Stretch);
        button.Click([this,action](auto&&,auto&&){try{error.Text(L"");action();}catch(const std::exception& e){error.Text(winrt::to_hstring(e.what()));}catch(...){error.Text(L"That did not work. Please try again.");}});
    }
    int CardIndex()const{const auto& cards=editor.CurrentScene().cards;for(int i=0;i<int(cards.size());++i)if(cards[i].id==cardId)return i;return -1;}
    void Open(const wchar_t* target){
        if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",target,nullptr,nullptr,SW_SHOWNORMAL))<=32)
            error.Text(L"Could not open the app. Open it yourself, then choose Callayers in its camera settings.");
    }
    void Finish(){
        if(!FinishSetup()){error.Text(L"Could not save setup progress. Please try again.");return;}
        active=false;Root.Visibility(setupui::Visibility::Collapsed);closed();
    }
    void AppGuide(){
        const wchar_t* labels[]{L"Open Google Meet",L"Open Zoom",L"Open Microsoft Teams",L"Open your calling app"};
        const wchar_t* tips[]{
            L"Open Meet, then open Settings → Video. Under Camera, choose Callayers (Windows Virtual Camera). You can check the picture before joining a meeting.",
            L"Open Zoom, then open Settings → Video. Under Camera, choose Callayers (Windows Virtual Camera). Check your name tag in the camera preview.",
            L"Open Teams, then open Settings → Devices (or its pre-join camera settings). Choose Callayers (Windows Virtual Camera) as your camera.",
            L"Open your calling app's video or camera settings. Choose Callayers (Windows Virtual Camera), then turn on its camera preview."};
        int index=std::max(0,apps.SelectedIndex());Text(guide,tips[index]);launch.Content(winrt::box_value(labels[index]));launch.Visibility(index==3?setupui::Visibility::Collapsed:setupui::Visibility::Visible);
    }
    void Refresh(){
        progress.Text(L"Step "+std::to_wstring(step+1)+L" of 3");
        const wchar_t* titles[]{L"Choose your camera",L"Create your first card",L"Try it in your calling app"};Text(title,titles[step]);
        const wchar_t* descriptions[]{L"Choose a camera and check your preview.",L"Add your name to a transparent name tag.",L"Choose Callayers as your camera. You can try it without joining a call."};Text(description,descriptions[step]);
        cameraStep.Visibility(step==0?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        cardStep.Visibility(step==1?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        connectStep.Visibility(step==2?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        back.Visibility(step>0?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        skip.Visibility(step==2?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        bool exists=CardIndex()>=0,hasName=std::wstring(name.Text()).find_first_not_of(L" \t\r\n")!=std::wstring::npos;
        next.Content(winrt::box_value(step==1&&!exists?L"Create name tag":L"Next"));
        next.IsEnabled(step==0?fresh:step==1?hasName:verified);
        next.Visibility((step==2&&!verified)||(step==0&&FAILED(cameraFailure))?setupui::Visibility::Collapsed:setupui::Visibility::Visible);
        launch.Visibility(step==2&&!verified&&apps.SelectedIndex()!=3?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        privacy.Visibility(cameraFailure==E_ACCESSDENIED?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        privacy.Content(winrt::box_value(permissionsOpened?L"Try again":L"Open camera permissions"));
        retry.Visibility(FAILED(cameraFailure)&&cameraFailure!=E_ACCESSDENIED?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        status.Visibility(step==2||FAILED(cameraFailure)||(step==0&&!fresh)?setupui::Visibility::Visible:setupui::Visibility::Collapsed);
        error.Visibility(error.Text().empty()?setupui::Visibility::Collapsed:setupui::Visibility::Visible);
    }
public:
    setupcontrols::ScrollViewer Root;
    SetupPanel(HWND owner,TextEditorUI& value,const std::vector<Device>& devices,std::function<void(int)> camera,std::function<void()> output,std::function<void()> onClose)
        :window(owner),editor(value),selectCamera(std::move(camera)),retryOutput(std::move(output)),closed(std::move(onClose)){
        Root.VerticalScrollBarVisibility(setupcontrols::ScrollBarVisibility::Auto);Root.HorizontalScrollBarVisibility(setupcontrols::ScrollBarVisibility::Disabled);
        body.Spacing(14);body.Padding({0,0,6,16});Root.Content(body);
        progress.FontSize(12);title.FontSize(24);title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        Text(progress,L"");Text(title,L"");Text(description,L"");Text(status,L"");Text(error,L"");
        body.Children().Append(progress);body.Children().Append(title);body.Children().Append(description);
        cameraStep.Spacing(10);cardStep.Spacing(10);connectStep.Spacing(10);
        cameras.Header(winrt::box_value(L"Your camera"));cameras.HorizontalAlignment(setupui::HorizontalAlignment::Stretch);
        cameras.Items().Append(winrt::box_value(L"Test pattern (no webcam)"));for(const auto& device:devices)cameras.Items().Append(winrt::box_value(device.name));cameras.SelectedIndex(0);
        cameras.SelectionChanged([this](auto&&,auto&&){if(!syncingCamera&&cameras.SelectedIndex()>=0){fresh=false;selectCamera(cameras.SelectedIndex());Refresh();}});
        cameraStep.Children().Append(cameras);
        Button(privacy,L"Open camera permissions",[this]{if(permissionsOpened){fresh=false;selectCamera(cameras.SelectedIndex());retryOutput();}else{Open(L"ms-settings:privacy-webcam");permissionsOpened=true;}Refresh();});Primary(privacy);cameraStep.Children().Append(privacy);
        Button(retry,L"Try camera again",[this]{fresh=false;selectCamera(cameras.SelectedIndex());retryOutput();});Primary(retry);cameraStep.Children().Append(retry);
        name.Header(winrt::box_value(L"Your name"));name.PlaceholderText(L"e.g. Alex Morgan");name.MaxLength(60);
        detail.Header(winrt::box_value(L"Second line (optional)"));detail.PlaceholderText(L"Role, company, or pronouns");detail.MaxLength(80);
        cardStep.Children().Append(name);cardStep.Children().Append(detail);
        name.TextChanged([this](auto&&,auto&&){try{EditTag();}catch(...){error.Text(L"Could not update your name tag. Please try again.");}});
        detail.TextChanged([this](auto&&,auto&&){try{EditTag();}catch(...){error.Text(L"Could not update your name tag. Please try again.");}});
        apps.Header(winrt::box_value(L"Calling app"));apps.HorizontalAlignment(setupui::HorizontalAlignment::Stretch);
        for(auto label:{L"Google Meet",L"Zoom",L"Microsoft Teams",L"Other"})apps.Items().Append(winrt::box_value(label));apps.SelectedIndex(0);
        apps.SelectionChanged([this](auto&&,auto&&){AppGuide();});connectStep.Children().Append(apps);
        Button(launch,L"Open Google Meet",[this]{const wchar_t* targets[]{L"https://meet.google.com/",L"zoommtg://zoom.us/",L"msteams://"};int index=apps.SelectedIndex();if(index>=0&&index<3)Open(targets[index]);});Primary(launch);connectStep.Children().Append(launch);
        guide.TextWrapping(setupui::TextWrapping::Wrap);connectStep.Children().Append(guide);AppGuide();
        body.Children().Append(cameraStep);body.Children().Append(cardStep);body.Children().Append(connectStep);
        status.FontSize(13);body.Children().Append(status);body.Children().Append(error);
        Button(next,L"Next",[this]{
            if(step==1&&CardIndex()<0){auto card=NameTagCard(std::wstring(name.Text()),std::wstring(detail.Text()));editor.AddSetupCard(card);cardId=card.id;Refresh();return;}
            if(step==1&&!editor.IsCardShown(cardId))editor.SetCardShown(CardIndex(),true);
            if(step<2){++step;Refresh();}else Finish();
        });Primary(next);body.Children().Append(next);
        Button(skip,L"I'll do this later",[this]{Finish();});Quiet(skip);body.Children().Append(skip);
        Button(back,L"Back",[this]{if(step>0)--step;Refresh();});Quiet(back);body.Children().Append(back);
        Root.Visibility(setupui::Visibility::Collapsed);
    }
    void Begin(int at=0){step=at;active=true;Root.Visibility(setupui::Visibility::Visible);Refresh();}
    bool Active()const{return active;}
    void CameraIndex(int index){syncingCamera=true;if(cameras.SelectedIndex()!=index)cameras.SelectedIndex(index);syncingCamera=false;}
    void Update(bool cameraFresh,bool outputReady,bool live,HRESULT failure,const std::wstring& message){
        fresh=cameraFresh;running=outputReady;cameraFailure=failure;delivering=live&&fresh&&running;if(delivering)verified=true;
        std::wstring text=delivering?L"Camera connected — live video is being delivered.":verified&&running?L"Ready for your next call. Choose Callayers in your calling app.":running?L"Ready for your calling app. Choose Callayers in its camera settings.":message;
        if(FAILED(failure))text=message;
        else if(!fresh)text=step==0?L"Waiting for your camera preview…":message;
        if(status.Text()!=text)status.Text(text);
        if(active)Refresh();
    }
    bool Verified()const{return verified;}
};
}
