#pragma once
#include "text_editor_ui.h"
#include "ai_client.h"
#include "cloud_client.h"
#include "card_host.h"
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <robuffer.h>
#include <MddBootstrap.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <Microsoft.UI.Dispatching.Interop.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <dwmapi.h>
#include "setup_panel.h"
#include "admin_panel.h"

namespace callayers {
namespace ux=winrt::Microsoft::UI::Xaml;
namespace controls=ux::Controls;
namespace hosting=ux::Hosting;
struct IslandApplication:ux::ApplicationT<IslandApplication,ux::Markup::IXamlMetadataProvider> {
    ux::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
    // Resources must be created after WindowsXamlManager initializes the UI thread.
    ux::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type){return provider.GetXamlType(type);}
    ux::Markup::IXamlType GetXamlType(winrt::hstring const& name){return provider.GetXamlType(name);}
    winrt::com_array<ux::Markup::XmlnsDefinition> GetXmlnsDefinitions(){return provider.GetXmlnsDefinitions();}
};
// WinUI owns the application chrome. The existing video surface remains a native
// sibling, so its pixels and local editing adorners never enter the XAML tree.
class WinUIShell {
    HWND window;
    TextEditorUI& editor;
    hosting::DesktopWindowXamlSource top{nullptr},bottom{nullptr},settingsIsland{nullptr};
    controls::ContentDialog settingsDialog{nullptr};
    controls::TextBlock accountStatus{nullptr};
    controls::Button accountSignin{nullptr},accountCancel{nullptr};
    bool accountGate=false;
    std::unique_ptr<AdminPanel> adminPanel;
    controls::Border colorPanel{nullptr};
    RECT colorPanelRegion{};
    std::shared_ptr<int> lifetime=std::make_shared<int>(0);
    ux::FrameworkElement header{nullptr},footer{nullptr};
    bool syncing=false,generating=false,annotating=false,composerOpen=false;
    std::vector<CardElement> listedCards;
    std::vector<controls::Border> cardRows;
    std::vector<controls::CheckBox> showChecks;
    AiJob job;
    std::unique_ptr<SetupPanel> setup;
    void ShowSetup(int step){
        auto grid=footer.as<controls::Grid>();for(auto child:grid.Children())child.Visibility(ux::Visibility::Collapsed);
        setup->Begin(step);
    }
    void CloseSetup(){
        auto grid=footer.as<controls::Grid>();for(auto child:grid.Children())child.Visibility(ux::Visibility::Visible);
        setup->Root.Visibility(ux::Visibility::Collapsed);Refresh();
    }
    std::optional<CardRequestTarget> lastTarget;
    std::wstring lastPrompt;
    std::string generationRequestId;
    void GenerationEvent(const wchar_t* state){
        using namespace winrt::Windows::Data::Json;JsonObject p;
        p.SetNamedValue(L"state",JsonValue::CreateStringValue(state));p.SetNamedValue(L"operation",JsonValue::CreateStringValue(lastTarget&&lastTarget->creating?L"create":L"revise"));
        if(!generationRequestId.empty())p.SetNamedValue(L"request_id",JsonValue::CreateStringValue(winrt::to_hstring(generationRequestId)));CloudTrack(L"card_generation",p);
    }
    template<class T> T Bottom(const wchar_t* name){return footer.FindName(name).as<T>();}
    void AiMessage(const std::wstring& message){Bottom<controls::TextBlock>(L"AiStatus").Text(message);}
    void RefreshComposer(){
        Bottom<controls::StackPanel>(L"Composer").Visibility(composerOpen&&!SetupActive()?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Bottom<controls::TextBlock>(L"PromptTarget").Text(editor.CardSelected()?L"Edit “"+editor.CardName()+L"”":L"New card");
        Bottom<controls::Button>(L"Generate").Content(winrt::box_value(generating?L"Working…":editor.CardSelected()?L"Update card":L"Generate card"));
        Bottom<controls::Button>(L"Generate").IsEnabled(!generating);
        Bottom<controls::Button>(L"CancelGeneration").Visibility(generating?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Bottom<controls::ProgressRing>(L"AiProgress").IsActive(generating);
        Bottom<controls::ProgressRing>(L"AiProgress").Visibility(generating?ux::Visibility::Visible:ux::Visibility::Collapsed);
    }
    void Generate(bool retry=false){
        try{
            if(generating)return;
            if(!retry){lastTarget=editor.GenerationTarget();lastPrompt=Bottom<controls::TextBox>(L"Prompt").Text();}
            if(!lastTarget||!editor.TargetCurrent(*lastTarget)){AiMessage(L"The original card or scene changed. Select a card and start a new request.");return;}
            // Retry is a deliberate new generation. Transport retries within CloudGenerate
            // recover the same server request; they never charge twice automatically.
            generationRequestId=CloudRequestId();job.Start(LoadAiSettings(),*lastTarget,lastPrompt,generationRequestId);generating=true;GenerationEvent(L"started");
            Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Collapsed);
            AiMessage(!WebsiteUrl(lastPrompt).empty()?L"Reading the website and creating your card…":lastTarget->creating?L"Creating your card…":L"Updating “"+lastTarget->card.name+L"”… Your current card stays visible.");RefreshComposer();
        }catch(const std::exception& e){AiMessage(std::wstring(winrt::to_hstring(e.what())));Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Visible);}
        catch(...){AiMessage(L"Could not start generation. Check AI settings and retry.");}
    }
    void PollGeneration(){
        if(!generating)return;auto result=job.TakeResult();if(!result)return;
        generating=false;
        try{
        if(result->card){
            GenerationEvent(L"succeeded");
            if(lastTarget&&editor.AcceptGenerated(*lastTarget,result->card->html,result->card->name,result->card->assets,result->card->usage)){composerOpen=true;AiMessage(std::wstring(editor.IsCardShown(lastTarget->card.id)?L"Card updated on camera. ":lastTarget->creating?L"Card ready. Check it to show on camera. ":L"Card updated. ")+UsageSummary(result->usage)+(result->notice.empty()?L"":L" · "+result->notice));if(Bottom<controls::TextBox>(L"Prompt").Text()==lastPrompt)Bottom<controls::TextBox>(L"Prompt").Text(L"");}
            else {GenerationEvent(L"discarded");AiMessage(L"The original card or scene changed. The late result was not applied.");}
        }else{GenerationEvent(L"failed");AiMessage(result->error+L" "+UsageSummary(result->usage));Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Visible);}
        }catch(const std::exception& e){AiMessage(std::wstring(winrt::to_hstring(e.what())));Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Visible);}
        catch(...){AiMessage(L"Could not apply this card. Retry with fewer images.");Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Visible);}
        RefreshComposer();
    }
    void ShowAiSettings(){
        if(CloudEnabled()){ShowAccount();return;}
        if(settingsIsland)return;
        auto settings=LoadAiSettings();settingsIsland=hosting::DesktopWindowXamlSource();settingsIsland.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        controls::Grid root;root.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,28,28,32}));settingsIsland.Content(root);
        RECT bounds;GetClientRect(window,&bounds);settingsIsland.SiteBridge().MoveAndResize({0,0,bounds.right,bounds.bottom});
        SetWindowPos(winrt::Microsoft::UI::GetWindowFromWindowId(settingsIsland.SiteBridge().WindowId()),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        controls::ContentDialog dialog;settingsDialog=dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(ux::ElementTheme::Dark);
        dialog.Title(winrt::box_value(L"OpenAI settings"));dialog.PrimaryButtonText(L"Save");dialog.CloseButtonText(L"Cancel");
        controls::StackPanel panel;panel.Spacing(12);panel.MinWidth(400);
        controls::TextBlock description;description.Text(L"Your prompt, website text, a compact style sample, image descriptions, and the card being revised are sent to OpenAI. Website retrieval fetches logos only; image files stay local; webcam frames and other cards are not sent.");description.TextWrapping(ux::TextWrapping::Wrap);panel.Children().Append(description);
        controls::TextBox model;model.Header(winrt::box_value(L"Model"));model.Text(settings.model);model.MaxLength(100);panel.Children().Append(model);
        controls::PasswordBox key;key.Header(winrt::box_value(L"OpenAI API key"));key.PlaceholderText(HasAiKey()?L"Key available — leave blank to keep it":L"Paste your API key");key.MaxLength(2000);panel.Children().Append(key);
        controls::TextBlock note;note.Text(L"Keys are saved in Windows Credential Manager, never in your deck. API billing is separate from ChatGPT.");note.TextWrapping(ux::TextWrapping::Wrap);panel.Children().Append(note);
        controls::TextBlock error;error.TextWrapping(ux::TextWrapping::Wrap);panel.Children().Append(error);dialog.Content(panel);
        dialog.PrimaryButtonClick([model,key,error](auto&&,controls::ContentDialogButtonClickEventArgs const& args){
            try{SaveAiSettings({std::wstring(model.Text())},std::wstring(key.Password()));key.Password(L"");}
            catch(const std::exception& e){error.Text(winrt::to_hstring(e.what()));args.Cancel(true);}
            catch(...){error.Text(L"Could not save the settings. Please try again.");args.Cancel(true);}
        });
        dialog.Closed([this,weak=std::weak_ptr<int>(lifetime)](auto&&,auto&&){
            if(weak.expired())return;
            header.DispatcherQueue().TryEnqueue([this,weak]{if(weak.expired())return;settingsDialog=nullptr;if(settingsIsland)settingsIsland.Close();settingsIsland=nullptr;SetFocus(window);});
        });
        dialog.ShowAsync();
    }

    void ShowAccount(){
        if(settingsIsland)return;
        settingsIsland=hosting::DesktopWindowXamlSource();settingsIsland.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        controls::Grid root;root.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,28,28,32}));settingsIsland.Content(root);
        RECT bounds;GetClientRect(window,&bounds);settingsIsland.SiteBridge().MoveAndResize({0,0,bounds.right,bounds.bottom});
        SetWindowPos(winrt::Microsoft::UI::GetWindowFromWindowId(settingsIsland.SiteBridge().WindowId()),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        controls::ContentDialog dialog;settingsDialog=dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(ux::ElementTheme::Dark);dialog.Title(winrt::box_value(L"Welcome to Callayers"));accountGate=!CloudAccessAllowed();dialog.CloseButtonText(accountGate?L"":L"Close");
        dialog.Closing([weak=std::weak_ptr<int>(lifetime)](auto&&,controls::ContentDialogClosingEventArgs const& args){if(!weak.expired()&&!CloudAccessAllowed())args.Cancel(true);});
        controls::StackPanel panel;panel.Spacing(12);panel.MinWidth(400);accountStatus=controls::TextBlock();accountStatus.TextWrapping(ux::TextWrapping::Wrap);panel.Children().Append(accountStatus);
        accountSignin=controls::Button();accountSignin.Content(winrt::box_value(L"Sign in with Google"));accountSignin.HorizontalAlignment(ux::HorizontalAlignment::Stretch);accountSignin.Click([](auto&&,auto&&){CloudSignIn();});panel.Children().Append(accountSignin);
        accountCancel=controls::Button();accountCancel.Content(winrt::box_value(L"Cancel sign-in"));accountCancel.Click([](auto&&,auto&&){CloudSignOut();});panel.Children().Append(accountCancel);
        controls::Expander details;details.Header(winrt::box_value(L"Privacy"));
        controls::TextBlock privacy;privacy.Text(L"AI prompts and selected card content are sent to Callayers and its AI provider. Webcam frames and image files stay local. Usage and error events help improve Callayers.");privacy.TextWrapping(ux::TextWrapping::Wrap);privacy.MaxWidth(420);details.Content(privacy);panel.Children().Append(details);
        dialog.Content(panel);dialog.Closed([this,weak=std::weak_ptr<int>(lifetime)](auto&&,auto&&){if(weak.expired())return;header.DispatcherQueue().TryEnqueue([this,weak]{if(weak.expired())return;accountStatus=nullptr;accountSignin=nullptr;accountCancel=nullptr;settingsDialog=nullptr;if(settingsIsland)settingsIsland.Close();settingsIsland=nullptr;SetFocus(window);});});
        UpdateSignIn();dialog.ShowAsync();
    }

    void UpdateSignIn(){
        if(!accountStatus)return;auto a=CloudGetAccount();
        auto text=a.email.empty()?a.message:a.email+L"\n"+a.message;
        if(accountStatus.Text()!=text)accountStatus.Text(text);
        accountSignin.IsEnabled(CloudEnabled()&&!a.busy);
        accountSignin.Content(winrt::box_value(a.busy?L"Waiting for Google…":L"Sign in with Google"));
        accountCancel.Visibility(a.busy?ux::Visibility::Visible:ux::Visibility::Collapsed);
    }

    void ShowAdmin(){
        auto account=CloudGetAccount();if(settingsIsland||!account.signedIn||!account.administrator)return;
        // Construct before creating the island so a request startup error leaves no modal behind.
        auto panel=std::make_unique<AdminPanel>();
        settingsIsland=hosting::DesktopWindowXamlSource();settingsIsland.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        controls::Grid root;root.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,28,28,32}));settingsIsland.Content(root);
        RECT bounds;GetClientRect(window,&bounds);settingsIsland.SiteBridge().MoveAndResize({0,0,bounds.right,bounds.bottom});
        SetWindowPos(winrt::Microsoft::UI::GetWindowFromWindowId(settingsIsland.SiteBridge().WindowId()),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        controls::ContentDialog dialog;settingsDialog=dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(ux::ElementTheme::Dark);
        dialog.Title(winrt::box_value(L"Callayers administration"));dialog.CloseButtonText(L"Close");dialog.Content(panel->Root);adminPanel=std::move(panel);
        dialog.Closed([this,weak=std::weak_ptr<int>(lifetime)](auto&&,auto&&){if(weak.expired())return;header.DispatcherQueue().TryEnqueue([this,weak]{if(weak.expired())return;adminPanel.reset();settingsDialog=nullptr;if(settingsIsland)settingsIsland.Close();settingsIsland=nullptr;SetFocus(window);});});
        dialog.ShowAsync();
    }

    template<class T> T Find(const wchar_t* name){return header.FindName(name).as<T>();}
    void Action(const wchar_t* name,std::function<void()> action){
        Find<controls::Button>(name).Click([this,action](auto&&,auto&&){try{action();}catch(...){EditorError(window);}});
    }
    void CloseColorPicker(){
        header.DispatcherQueue().TryEnqueue([this,weak=std::weak_ptr<int>(lifetime)]{
            if(weak.expired()||!colorPanel)return;colorPanel=nullptr;if(settingsIsland)settingsIsland.Close();settingsIsland=nullptr;colorPanelRegion={};
            InvalidateRect(window,nullptr,FALSE);SetFocus(window);
        });
    }
    void ClipColorPicker(){
        if(!colorPanel||!settingsIsland||colorPanel.ActualWidth()<=0||colorPanel.ActualHeight()<=0)return;
        auto root=settingsIsland.Content().as<ux::FrameworkElement>();auto point=colorPanel.TransformToVisual(root).TransformPoint({0,0});double scale=root.XamlRoot().RasterizationScale();
        RECT next{LONG(std::floor(point.X*scale)),LONG(std::floor(point.Y*scale)),LONG(std::ceil((point.X+colorPanel.ActualWidth())*scale)),LONG(std::ceil((point.Y+colorPanel.ActualHeight())*scale))};
        if(EqualRect(&next,&colorPanelRegion))return;colorPanelRegion=next;
        // Clip the native island too: a transparent XAML brush alone cannot reveal
        // the sibling native camera surface behind a full-window island.
        auto region=CreateRoundRectRgn(next.left,next.top,next.right+1,next.bottom+1,int(16*scale),int(16*scale));
        if(!SetWindowRgn(winrt::Microsoft::UI::GetWindowFromWindowId(settingsIsland.SiteBridge().WindowId()),region,TRUE))DeleteObject(region);
        InvalidateRect(window,nullptr,FALSE);
    }
    void ChooseAnnotationColor(){
        if(settingsIsland)return;
        // Lay out against the full window, but expose only the picker panel.
        settingsIsland=hosting::DesktopWindowXamlSource();settingsIsland.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        controls::Grid root;root.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{0,0,0,0}));settingsIsland.Content(root);
        RECT bounds;GetClientRect(window,&bounds);settingsIsland.SiteBridge().MoveAndResize({0,0,bounds.right,bounds.bottom});
        SetWindowPos(winrt::Microsoft::UI::GetWindowFromWindowId(settingsIsland.SiteBridge().WindowId()),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        auto hiddenRegion=CreateRectRgn(0,0,0,0);if(!SetWindowRgn(winrt::Microsoft::UI::GetWindowFromWindowId(settingsIsland.SiteBridge().WindowId()),hiddenRegion,TRUE))DeleteObject(hiddenRegion);
        root.RequestedTheme(ux::ElementTheme::Dark);
        auto current=editor.AnnotationColor();controls::ColorPicker picker;picker.Color(winrt::Windows::UI::Color{255,BYTE(current>>16),BYTE(current>>8),BYTE(current)});
        picker.IsAlphaEnabled(false);picker.IsMoreButtonVisible(false);picker.IsColorChannelTextInputVisible(false);picker.IsHexInputVisible(true);picker.Width(240);picker.HorizontalAlignment(ux::HorizontalAlignment::Center);picker.ColorSpectrumShape(controls::ColorSpectrumShape::Box);
        controls::StackPanel panel;panel.Spacing(10);panel.Width(280);
        controls::TextBlock title;title.Text(L"Annotation color");title.FontSize(20);title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());title.Margin({0,0,0,8});panel.Children().Append(title);
        const uint32_t colors[]{0x000000,0x808080,0xffffff,0xff0000,0xff8000,0xffff00,0x00a000,0x00bfff,0x0000ff,0x8000ff,0xff00ff,0x804000};
        for(int row=0;row<2;++row){controls::StackPanel swatches;swatches.Orientation(controls::Orientation::Horizontal);swatches.Spacing(8);
            for(int column=0;column<6;++column){auto rgb=colors[row*6+column];auto color=winrt::Windows::UI::Color{255,BYTE(rgb>>16),BYTE(rgb>>8),BYTE(rgb)};
                controls::Button swatch;swatch.Width(40);swatch.Height(28);swatch.MinHeight(28);swatch.Padding({0,0,0,0});swatch.Background(ux::Media::SolidColorBrush(color));swatch.BorderBrush(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,160,160,160}));swatch.BorderThickness({1,1,1,1});
                wchar_t label[8];swprintf_s(label,L"#%06X",rgb);controls::ToolTipService::SetToolTip(swatch,winrt::box_value(label));ux::Automation::AutomationProperties::SetName(swatch,label);
                swatch.Click([picker,color](auto&&,auto&&){picker.Color(color);});swatches.Children().Append(swatch);
            }panel.Children().Append(swatches);
        }
        panel.Children().Append(picker);
        controls::StackPanel actions;actions.Orientation(controls::Orientation::Horizontal);actions.Spacing(8);actions.Margin({0,12,0,0});actions.HorizontalAlignment(ux::HorizontalAlignment::Right);
        controls::Button apply;apply.Content(winrt::box_value(L"Apply"));apply.Click([this,picker](auto&&,auto&&){try{auto color=picker.Color();editor.SetAnnotationColor((uint32_t(color.R)<<16)|(uint32_t(color.G)<<8)|color.B);CloseColorPicker();}catch(...){EditorError(window);}});actions.Children().Append(apply);
        controls::Button cancel;cancel.Content(winrt::box_value(L"Cancel"));cancel.Click([this](auto&&,auto&&){CloseColorPicker();});actions.Children().Append(cancel);panel.Children().Append(actions);
        colorPanel=controls::Border();colorPanel.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,40,40,40}));colorPanel.BorderBrush(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,65,65,65}));colorPanel.BorderThickness({1,1,1,1});colorPanel.CornerRadius({8,8,8,8});colorPanel.Padding({24,20,24,20});colorPanel.HorizontalAlignment(ux::HorizontalAlignment::Center);colorPanel.VerticalAlignment(ux::VerticalAlignment::Center);colorPanel.Child(panel);root.Children().Append(colorPanel);
        root.LayoutUpdated([this,weak=std::weak_ptr<int>(lifetime)](auto&&,auto&&){if(!weak.expired())ClipColorPicker();});
        root.KeyDown([this](auto&&,ux::Input::KeyRoutedEventArgs const& args){if(args.Key()==winrt::Windows::System::VirtualKey::Escape){CloseColorPicker();args.Handled(true);}});
        root.UpdateLayout();ClipColorPicker();picker.Focus(ux::FocusState::Programmatic);
    }
    void Relayout(){RECT r;GetClientRect(window,&r);SendMessageW(window,WM_SIZE,0,MAKELPARAM(r.right,r.bottom));}
    void OpenComposer(bool fresh){if(fresh)editor.NewCardSelection();composerOpen=true;RefreshComposer();Bottom<controls::TextBox>(L"Prompt").Focus(ux::FocusState::Programmatic);}
    void RefreshCards(){
        const auto& cards=editor.CurrentScene().cards;
        if(cards!=listedCards){
            listedCards=cards;auto list=Bottom<controls::StackPanel>(L"Cards");list.Children().Clear();cardRows.clear();showChecks.clear();
            for(size_t i=0;i<cards.size();++i){
                controls::Border row;row.CornerRadius({6,6,6,6});row.BorderThickness({1,1,1,1});row.Padding({8,6,8,6});
                controls::Grid content;content.ColumnSpacing(6);
                for(auto width:{ux::GridLength{1,ux::GridUnitType::Auto},ux::GridLength{1,ux::GridUnitType::Star},ux::GridLength{1,ux::GridUnitType::Auto}}){controls::ColumnDefinition column;column.Width(width);content.ColumnDefinitions().Append(column);}
                controls::CheckBox show;show.MinWidth(0);show.VerticalAlignment(ux::VerticalAlignment::Center);show.IsChecked(editor.IsCardShown(cards[i].id));ux::Automation::AutomationProperties::SetName(show,L"Show "+cards[i].name+L" on camera");controls::ToolTipService::SetToolTip(show,winrt::box_value(L"Show on camera. Check multiple cards to show them together."));
                show.Click([this,index=int(i)](winrt::Windows::Foundation::IInspectable const& sender,auto&&){if(syncing)return;try{editor.SetCardShown(index,sender.as<controls::CheckBox>().IsChecked().Value());}catch(...){EditorError(window);}});content.Children().Append(show);showChecks.push_back(show);
                controls::Grid titleArea;controls::Grid::SetColumn(titleArea,1);titleArea.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{0,0,0,0}));
                controls::TextBlock title;title.Text(cards[i].name);title.TextTrimming(ux::TextTrimming::CharacterEllipsis);title.VerticalAlignment(ux::VerticalAlignment::Center);title.Padding({0,6,0,6});titleArea.Children().Append(title);
                controls::TextBox rename;rename.Text(cards[i].name);rename.MaxLength(100);rename.MinWidth(0);rename.Padding({4,4,4,4});rename.Visibility(ux::Visibility::Collapsed);ux::Automation::AutomationProperties::SetName(rename,L"Card title");titleArea.Children().Append(rename);
                auto editing=std::make_shared<bool>(false);auto id=cards[i].id;auto weakRename=winrt::make_weak(rename);auto weakTitle=winrt::make_weak(title);
                auto finish=[this,weakRename,weakTitle,editing,id](bool save){if(!*editing)return;*editing=false;auto field=weakRename.get();auto label=weakTitle.get();if(!field||!label)return;auto value=std::wstring(field.Text());field.Visibility(ux::Visibility::Collapsed);label.Visibility(ux::Visibility::Visible);if(save)try{editor.RenameCard(id,value);}catch(...){EditorError(window);}};
                auto beginRename=[weakRename,weakTitle,editing]{auto field=weakRename.get();auto label=weakTitle.get();if(!field||!label)return;*editing=true;field.Text(label.Text());label.Visibility(ux::Visibility::Collapsed);field.Visibility(ux::Visibility::Visible);field.Focus(ux::FocusState::Programmatic);field.SelectAll();};
                titleArea.Tapped([this,index=int(i),editing](auto&&,ux::Input::TappedRoutedEventArgs const& args){if(*editing)return;try{editor.SelectCard(index);composerOpen=true;RefreshComposer();args.Handled(true);}catch(...){EditorError(window);}});
                title.DoubleTapped([beginRename](auto&&,ux::Input::DoubleTappedRoutedEventArgs const& args){beginRename();args.Handled(true);});
                rename.KeyDown([finish](auto&&,ux::Input::KeyRoutedEventArgs const& args){if(args.Key()==winrt::Windows::System::VirtualKey::Enter||args.Key()==winrt::Windows::System::VirtualKey::Escape){finish(args.Key()==winrt::Windows::System::VirtualKey::Enter);args.Handled(true);}});
                rename.LostFocus([finish](auto&&,auto&&){finish(true);});content.Children().Append(titleArea);
                controls::Button more;more.Content(winrt::box_value(L"···"));more.Padding({6,4,6,4});controls::Grid::SetColumn(more,2);controls::ToolTipService::SetToolTip(more,winrt::box_value(L"Card actions"));controls::MenuFlyout menu;
                controls::MenuFlyoutItem renameItem;renameItem.Text(L"Rename");renameItem.Click([beginRename](auto&&,auto&&){beginRename();});menu.Items().Append(renameItem);
                for(auto entry:{std::pair{L"Move up",-1},std::pair{L"Move down",1},std::pair{L"Delete card",0}}){controls::MenuFlyoutItem item;item.Text(entry.first);item.Click([this,index=int(i),direction=entry.second](auto&&,auto&&){try{editor.SelectCard(index);if(direction)editor.MoveSelectedCard(direction);else editor.Command(TextEditorUI::DeleteId);}catch(...){EditorError(window);}});menu.Items().Append(item);}more.Flyout(menu);content.Children().Append(more);
                row.Child(content);list.Children().Append(row);cardRows.push_back(row);
            }
        }
        Bottom<controls::TextBlock>(L"EmptyCards").Visibility(cards.empty()?ux::Visibility::Visible:ux::Visibility::Collapsed);
        for(size_t i=0;i<cardRows.size();++i){bool live=editor.IsCardShown(cards[i].id),selected=int(i)==editor.SelectedCardIndex();
            cardRows[i].BorderBrush(ux::Media::SolidColorBrush(selected?winrt::Windows::UI::Color{255,163,155,250}:winrt::Windows::UI::Color{255,57,56,63}));
            cardRows[i].Background(ux::Media::SolidColorBrush(live?winrt::Windows::UI::Color{255,41,60,51}:winrt::Windows::UI::Color{255,36,36,40}));
            showChecks[i].IsChecked(live);
        }
        Bottom<controls::Button>(L"HideCard").IsEnabled(editor.HasShownCards());
    }
public:
    WinUIShell(HWND owner,TextEditorUI& text,const std::vector<Device>& devices,std::function<void(int)> camera,std::function<void()> output):window(owner),editor(text){
        top=hosting::DesktopWindowXamlSource();bottom=hosting::DesktopWindowXamlSource();
        auto id=winrt::Microsoft::UI::GetWindowIdFromWindow(window);
        top.Initialize(id);bottom.Initialize(id);
        header=ux::Markup::XamlReader::Load(LR"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RequestedTheme="Dark" Background="#242428">
 <Grid.Resources>
  <SolidColorBrush x:Key="SystemControlHighlightAccentBrush" Color="#A39BFA"/>
  <Style TargetType="Button"><Setter Property="CornerRadius" Value="6"/><Setter Property="Padding" Value="12,7"/><Setter Property="MinHeight" Value="36"/><Setter Property="Height" Value="36"/><Setter Property="VerticalAlignment" Value="Center"/></Style>
 </Grid.Resources>
 <Grid.RowDefinitions><RowDefinition Height="72"/><RowDefinition Height="56"/></Grid.RowDefinitions>
 <Grid Padding="24,0" ColumnSpacing="18">
  <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
  <StackPanel Orientation="Horizontal" Spacing="11" VerticalAlignment="Center">
   <Viewbox Width="31" Height="31"><Canvas Width="100" Height="100"><Rectangle Width="100" Height="100" RadiusX="20" RadiusY="20" Fill="#5E78F5"/><Path Data="M 60 82 C 63.3 82 66 79.3 66 76 L 66 73 C 66 71.3 64.7 70 63 70 L 30 70 C 28.3 70 27 68.7 27 67 L 27 46 C 27 44.3 28.3 43 30 43 L 40 43 L 40 31 L 24 31 C 19 31 15 35 15 40 L 15 73 C 15 78 19 82 24 82 Z" Fill="White"/><Path Data="M 77 65 C 80.3 65 83 62.3 83 59 L 83 56 C 83 54.3 81.7 53 80 53 L 47 53 C 45.3 53 44 51.7 44 50 L 44 29 C 44 27.3 45.3 26 47 26 L 80 26 C 81.7 26 83 24.7 83 23 L 83 20 C 83 16.7 80.3 14 77 14 L 41 14 C 36 14 32 18 32 23 L 32 56 C 32 61 36 65 41 65 Z" Fill="White"/></Canvas></Viewbox>
   <TextBlock Text="Callayers" FontSize="21" FontWeight="SemiBold" VerticalAlignment="Center"/>
  </StackPanel>
  <StackPanel Grid.Column="1" VerticalAlignment="Center" Spacing="2" Margin="12,0,0,0">
   <TextBlock x:Name="DeckName" Text="Untitled deck" FontSize="14" TextTrimming="CharacterEllipsis"/>
   <TextBlock x:Name="SaveState" Text="No unsaved changes" Foreground="#AAA8B2" FontSize="11"/>
  </StackPanel>
  <StackPanel Grid.Column="2" Orientation="Horizontal" Spacing="6" VerticalAlignment="Center">
   <Button x:Name="FileButton" Content="File"><Button.Flyout><MenuFlyout><MenuFlyoutItem x:Name="NewDeck" Text="New deck"/><MenuFlyoutItem x:Name="OpenDeck" Text="Open…"/><MenuFlyoutSeparator/><MenuFlyoutItem x:Name="SaveDeck" Text="Save"/><MenuFlyoutItem x:Name="SaveAsDeck" Text="Save as…"/><MenuFlyoutSeparator/><MenuFlyoutItem x:Name="Administration" Text="Administration…" Visibility="Collapsed"/><MenuFlyoutItem x:Name="Account" Text="Log out"/><MenuFlyoutItem x:Name="AiSettings" Text="Settings…"/></MenuFlyout></Button.Flyout></Button>
   <Button x:Name="Annotate" Content="✎  Annotate" ToolTipService.ToolTip="Show or hide the annotation toolbar"/>
   <Button x:Name="Save" Content="Save" ToolTipService.ToolTip="Save deck (Ctrl+S)"/>
  </StackPanel>
  <StackPanel Grid.Column="3" Orientation="Horizontal" Spacing="8" VerticalAlignment="Center">
   <ComboBox x:Name="Camera" Width="188" MaxDropDownHeight="300" PlaceholderText="Camera" AutomationProperties.Name="Camera source"/>
   <Button x:Name="Output" Content="Start camera" Background="#A39BFA" Foreground="#201D33" BorderThickness="0" MinWidth="118" ToolTipService.ToolTip="Make Callayers available as a camera in your meeting app"/>
  </StackPanel>
 </Grid>
 <Border x:Name="MarkupBar" Grid.Row="1" BorderBrush="#39383F" BorderThickness="0,1,0,1" Padding="12,0" Margin="300,0,0,0" Visibility="Collapsed">
  <ScrollViewer HorizontalScrollBarVisibility="Auto" VerticalScrollBarVisibility="Disabled" HorizontalScrollMode="Enabled">
   <StackPanel Orientation="Horizontal" Spacing="5" VerticalAlignment="Center">
    <Button x:Name="Select" Content="↖" ToolTipService.ToolTip="Select, move, and resize annotations" AutomationProperties.Name="Select"/>
    <Button x:Name="Pen" Content="✎" ToolTipService.ToolTip="Pen (P)" AutomationProperties.Name="Pen"/>
    <Button x:Name="Shapes" Content="□  ▾" ToolTipService.ToolTip="Shapes" AutomationProperties.Name="Shapes"><Button.Flyout><MenuFlyout><MenuFlyoutItem x:Name="ArrowTool" Text="Arrow"/><MenuFlyoutItem x:Name="RectangleTool" Text="Rectangle"/><MenuFlyoutItem x:Name="EllipseTool" Text="Ellipse"/></MenuFlyout></Button.Flyout></Button>
    <Button x:Name="Text" Content="T" ToolTipService.ToolTip="Text (T)" AutomationProperties.Name="Text"/>
    <Border Width="1" Height="22" Background="#49474F" Margin="6,0"/>
    <Button x:Name="InkColor" ToolTipService.ToolTip="Annotation color" AutomationProperties.Name="Annotation color"><StackPanel Orientation="Horizontal" Spacing="8"><Border x:Name="ColorSwatch" Width="22" Height="22" CornerRadius="3" BorderThickness="1" BorderBrush="#AAAAAA" Background="White"/><FontIcon Glyph="&#xE70D;" FontSize="10"/></StackPanel></Button>
    <ComboBox x:Name="InkWidth" Height="36" MinHeight="36" VerticalAlignment="Center" Width="110" ToolTipService.ToolTip="Stroke width or text size" AutomationProperties.Name="Size"><x:String>Small</x:String><x:String>Medium</x:String><x:String>Large</x:String></ComboBox>
    <Border Width="1" Height="22" Background="#49474F" Margin="6,0"/>
    <Button x:Name="Undo" ToolTipService.ToolTip="Undo (Ctrl+Z)" AutomationProperties.Name="Undo"><SymbolIcon Symbol="Undo"/></Button>
    <Button x:Name="Redo" ToolTipService.ToolTip="Redo (Ctrl+Y)" AutomationProperties.Name="Redo"><SymbolIcon Symbol="Redo"/></Button>
    <Button x:Name="Clear" Content="Clear" ToolTipService.ToolTip="Clear all temporary annotations"/>
   </StackPanel>
  </ScrollViewer>
 </Border>
</Grid>)XAML").as<ux::FrameworkElement>();
        footer=ux::Markup::XamlReader::Load(LR"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RequestedTheme="Dark" Background="#242428" Padding="16,16,16,10" RowSpacing="10">
 <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
 <StackPanel Spacing="8">
  <TextBlock Text="Cards" FontSize="20" FontWeight="SemiBold"/>
  <Button x:Name="NewCard" Height="40" MinHeight="40" Padding="12,8" VerticalContentAlignment="Center" Content="＋ Create with AI" HorizontalAlignment="Stretch" Background="#A39BFA" Foreground="#201D33"/>
  <StackPanel Orientation="Horizontal" Spacing="8"><Button x:Name="ImportImage" Content="Import image" Padding="10,5"/><Button x:Name="HideCard" Content="Hide all" Padding="10,5"/></StackPanel>
 </StackPanel>
 <ScrollViewer Grid.Row="1" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
  <StackPanel Spacing="8"><TextBlock x:Name="EmptyCards" Text="Make a card with AI, or drop an image onto the stage. Check cards to show them together on camera." TextWrapping="Wrap" Foreground="#AAA8B2" Margin="0,12"/><StackPanel x:Name="Cards" Spacing="10"/></StackPanel>
 </ScrollViewer>
 <StackPanel Grid.Row="2" x:Name="Composer" Spacing="8" Visibility="Collapsed">
  <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><TextBlock x:Name="PromptTarget" Text="Create with AI" FontWeight="SemiBold" TextTrimming="CharacterEllipsis" VerticalAlignment="Center"/><Button x:Name="CloseComposer" Grid.Column="1" Content="×" Padding="8,2" ToolTipService.ToolTip="Close composer" AutomationProperties.Name="Close composer"/></Grid>
  <TextBox x:Name="Prompt" PlaceholderText="Describe your card, paste a website, or describe a change…" MaxLength="4000" AcceptsReturn="True" TextWrapping="Wrap" Height="90" AutomationProperties.Name="Card prompt"/>
  <StackPanel Orientation="Horizontal" Spacing="6"><Button x:Name="Generate" Content="Generate card" Background="#A39BFA" Foreground="#201D33"/><Button x:Name="CancelGeneration" Content="Cancel" Visibility="Collapsed"/></StackPanel>
 </StackPanel>
 <StackPanel Grid.Row="3" Spacing="6">
  <Button x:Name="UseInCall" Content="Use in a call" HorizontalAlignment="Stretch"/>
  <TextBlock x:Name="ConnectionStatus" Text="Preparing camera…" FontSize="12" Foreground="#C4C0D2" TextWrapping="Wrap"/>
  <StackPanel Orientation="Horizontal" Spacing="6"><ProgressRing x:Name="AiProgress" Width="18" Height="18" IsActive="False" Visibility="Collapsed"/><Button x:Name="RetryGeneration" Content="Retry" Padding="8,2" Visibility="Collapsed"/></StackPanel>
  <TextBlock x:Name="AiStatus" Text="Check cards to show them. Click a title to edit; double-click to rename." FontSize="11" Foreground="#C4C0D2" TextWrapping="Wrap" MaxHeight="68" IsTextSelectionEnabled="True"/>
  <TextBlock x:Name="Status" Text="Camera output is off" FontSize="11" Foreground="#9692A4" TextWrapping="Wrap" MaxHeight="48"/>
 </StackPanel>
</Grid>)XAML").as<ux::FrameworkElement>();
        top.Content(header);bottom.Content(footer);
        top.TakeFocusRequested([this](auto&&,auto&&){SetFocus(window);});
        bottom.TakeFocusRequested([this](auto&&,auto&&){SetFocus(window);});
        for(auto pair:{std::pair{L"Select",TextEditorUI::SelectId},{L"Text",TextEditorUI::TextId},{L"Undo",TextEditorUI::UndoId},{L"Redo",TextEditorUI::RedoId},{L"Save",TextEditorUI::SaveId}})
            Action(pair.first,[this,id=pair.second]{editor.Command(id);});
        for(auto pair:{std::pair{L"NewDeck",TextEditorUI::NewIdControl},{L"OpenDeck",TextEditorUI::OpenId},{L"SaveDeck",TextEditorUI::SaveId}})
            Find<controls::MenuFlyoutItem>(pair.first).Click([this,id=pair.second](auto&&,auto&&){try{editor.Command(id);}catch(...){EditorError(window);}});
        Find<controls::MenuFlyoutItem>(L"SaveAsDeck").Click([this](auto&&,auto&&){try{editor.SaveAs();}catch(...){EditorError(window);}});
        auto sources=Find<controls::ComboBox>(L"Camera");sources.Items().Append(winrt::box_value(L"Test pattern"));
        for(auto& device:devices)sources.Items().Append(winrt::box_value(device.name));sources.SelectedIndex(0);
        sources.SelectionChanged([this,camera](auto&&,auto&&){if(!syncing)try{int index=Find<controls::ComboBox>(L"Camera").SelectedIndex();camera(index);if(setup)setup->CameraIndex(index);}catch(...){EditorError(window);}});
        Action(L"Output",output);
        Action(L"Annotate",[this]{annotating=!annotating;editor.SetAnnotationMode(annotating);Refresh();Relayout();});
        Action(L"Pen",[this]{editor.SetDrawingTool(TextEditorUI::DrawTool::Pen);});
        Action(L"Clear",[this]{editor.ClearAnnotations();});
        for(auto pair:{std::pair{L"ArrowTool",TextEditorUI::DrawTool::Arrow},{L"RectangleTool",TextEditorUI::DrawTool::Rectangle},{L"EllipseTool",TextEditorUI::DrawTool::Ellipse}})
            Find<controls::MenuFlyoutItem>(pair.first).Click([this,tool=pair.second](auto&&,auto&&){try{editor.SetDrawingTool(tool);}catch(...){EditorError(window);}});
        Action(L"InkColor",[this]{ChooseAnnotationColor();});
        Find<controls::ComboBox>(L"InkWidth").SelectionChanged([this](auto&&,auto&&){if(syncing)return;try{auto index=Find<controls::ComboBox>(L"InkWidth").SelectedIndex();if(index>=0)editor.SetAnnotationSize(index);}catch(...){EditorError(window);}});
        Bottom<controls::Button>(L"NewCard").Click([this](auto&&,auto&&){try{OpenComposer(true);}catch(...){EditorError(window);}});
        Bottom<controls::Button>(L"CloseComposer").Click([this](auto&&,auto&&){composerOpen=false;RefreshComposer();});
        Bottom<controls::Button>(L"ImportImage").Click([this](auto&&,auto&&){try{editor.ChooseImage();if(editor.CardSelected())OpenComposer(false);}catch(...){EditorError(window);}});
        Bottom<controls::Button>(L"HideCard").Click([this](auto&&,auto&&){try{editor.HideCard();}catch(...){EditorError(window);}});
        Bottom<controls::Button>(L"Generate").Click([this](auto&&,auto&&){Generate();});
        Bottom<controls::Button>(L"RetryGeneration").Click([this](auto&&,auto&&){Generate(true);});
        Bottom<controls::Button>(L"CancelGeneration").Click([this](auto&&,auto&&){GenerationEvent(L"canceled");job.Cancel();generating=false;AiMessage(L"Canceled locally. An AI request already submitted may still use your allowance. Your cards are unchanged.");RefreshComposer();});
        Find<controls::MenuFlyoutItem>(L"Account").Text(L"Log out");
        Find<controls::MenuFlyoutItem>(L"Account").Visibility(CloudEnabled()?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Find<controls::MenuFlyoutItem>(L"Account").Click([this](auto&&,auto&&){try{CloudSignOut();SyncAccess();}catch(...){EditorError(window);}});
        Find<controls::MenuFlyoutItem>(L"Administration").Click([this](auto&&,auto&&){try{ShowAdmin();}catch(...){EditorError(window);}});
        Find<controls::MenuFlyoutItem>(L"AiSettings").Visibility(CloudEnabled()?ux::Visibility::Collapsed:ux::Visibility::Visible);
        Find<controls::MenuFlyoutItem>(L"AiSettings").Click([this](auto&&,auto&&){try{ShowAiSettings();}catch(...){EditorError(window);}});
        Bottom<controls::TextBox>(L"Prompt").KeyDown([this](auto&&,ux::Input::KeyRoutedEventArgs const& args){if(args.Key()==winrt::Windows::System::VirtualKey::Enter&&(GetKeyState(VK_CONTROL)&0x8000)){Generate();args.Handled(true);}});
        setup=std::make_unique<SetupPanel>(window,editor,devices,[this,camera](int index){syncing=true;Find<controls::ComboBox>(L"Camera").SelectedIndex(index);syncing=false;camera(index);},output,[this]{CloseSetup();});
        controls::Grid::SetRowSpan(setup->Root,4);footer.as<controls::Grid>().Children().Append(setup->Root);
        Bottom<controls::Button>(L"UseInCall").Click([this](auto&&,auto&&){ShowSetup(2);});
        editor.StateChanged=[this]{Refresh();};Refresh();
        if(!SetupFinished())ShowSetup(0);
        SyncAccess();
        BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    }
    ~WinUIShell(){lifetime.reset();job.Cancel();editor.StateChanged={};if(settingsDialog)settingsDialog.Hide();if(settingsIsland)settingsIsland.Close();if(top)top.Close();if(bottom)bottom.Close();}
    void Layout(int width,int height,double scale){
        if(settingsIsland)settingsIsland.SiteBridge().MoveAndResize({0,0,width,height});
        int topHeight=static_cast<int>(HeaderHeight()*scale),sidebar=static_cast<int>(300*scale),headerHeight=static_cast<int>(72*scale);
        top.SiteBridge().MoveAndResize({0,0,width,topHeight});
        // Exclude the sidebar from the lower header row; sibling islands otherwise
        // cover its heading and Create button with the header's opaque background.
        auto region=CreateRectRgn(0,0,width,headerHeight);if(annotating){auto toolbar=CreateRectRgn(sidebar,headerHeight,width,topHeight);CombineRgn(region,region,toolbar,RGN_OR);DeleteObject(toolbar);}
        if(!SetWindowRgn(winrt::Microsoft::UI::GetWindowFromWindowId(top.SiteBridge().WindowId()),region,TRUE))DeleteObject(region);
        bottom.SiteBridge().MoveAndResize({0,headerHeight,sidebar,std::max(1,height-headerHeight)});
    }
    int HeaderHeight()const{return annotating?128:72;}
    void CameraIndex(int index){syncing=true;Find<controls::ComboBox>(L"Camera").SelectedIndex(index);syncing=false;if(setup)setup->CameraIndex(index);}
    bool SetupActive()const{return setup&&setup->Active();}
    void SetupStatus(bool fresh,bool running,bool live,HRESULT failure,const std::wstring& message){
        setup->Update(fresh,running,live,failure,message);
        auto label=Bottom<controls::TextBlock>(L"ConnectionStatus");
        std::wstring text=live&&fresh&&running?L"● Camera in use · Live video delivered":!running?L"Camera output unavailable":setup->Verified()?L"Ready for your next call":L"Ready · Choose Callayers in your calling app";
        if(FAILED(failure))text=L"Camera needs attention";
        if(label.Text()!=text)label.Text(text);
    }
    void SyncAccess(){
        if(CloudTakeSignInReturn()){
            ShowWindow(window,IsIconic(window)?SW_RESTORE:SW_SHOW);
            if(!SetForegroundWindow(window)){FLASHWINFO flash{sizeof(FLASHWINFO),window,FLASHW_TRAY,3,0};FlashWindowEx(&flash);}
        }
        const bool allowed=CloudAccessAllowed();
        const auto visibility=allowed?ux::Visibility::Visible:ux::Visibility::Collapsed;
        header.Visibility(visibility);footer.Visibility(visibility);
        if(!allowed){
            if(generating){job.Cancel();generating=false;RefreshComposer();}
            if(accountStatus){accountGate=true;settingsDialog.CloseButtonText(L"");}
            else if(settingsDialog)settingsDialog.Hide();
            else if(colorPanel)CloseColorPicker();
            else if(!settingsIsland)ShowAccount();
        }else if(accountStatus&&accountGate){accountGate=false;settingsDialog.CloseButtonText(L"Close");settingsDialog.Hide();}
    }
    void Status(const std::wstring& message,bool busy,bool running){
        UpdateSignIn();
        auto account=CloudGetAccount();Find<controls::MenuFlyoutItem>(L"Administration").Visibility(account.signedIn&&account.administrator?ux::Visibility::Visible:ux::Visibility::Collapsed);
        if(adminPanel)adminPanel->Poll();
        PollGeneration();
        auto status=footer.FindName(L"Status").as<controls::TextBlock>();if(status.Text()!=message)status.Text(message);
        auto button=Find<controls::Button>(L"Output");button.IsEnabled(!busy&&!running);
        winrt::hstring label=busy?L"Starting…":running?L"Camera available":L"Retry camera";
        if(winrt::unbox_value<winrt::hstring>(button.Content())!=label)button.Content(winrt::box_value(label));
        auto name=Find<controls::TextBlock>(L"DeckName");if(name.Text()!=editor.Name())name.Text(editor.Name());
        auto save=Find<controls::TextBlock>(L"SaveState");winrt::hstring saved=editor.Dirty()?L"Unsaved changes":L"No unsaved changes";if(save.Text()!=saved)save.Text(saved);
    }
    void Refresh(){
        syncing=true;auto tool=editor.DrawingTool();
        if(!annotating&&(editor.TextTool()||tool!=TextEditorUI::DrawTool::None)){annotating=true;editor.EnableAnnotationSelection();Relayout();}
        Find<controls::Border>(L"MarkupBar").Visibility(annotating?ux::Visibility::Visible:ux::Visibility::Collapsed);
        auto activeBrush=ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,76,69,116}),idleBrush=ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,48,47,53});
        Find<controls::Button>(L"Annotate").Background(annotating?activeBrush:idleBrush);
        Find<controls::Button>(L"Select").Background(tool==TextEditorUI::DrawTool::None&&!editor.TextTool()?activeBrush:idleBrush);
        Find<controls::Button>(L"Text").Background(editor.TextTool()?activeBrush:idleBrush);
        Find<controls::Button>(L"Pen").Background(tool==TextEditorUI::DrawTool::Pen?activeBrush:idleBrush);
        bool shape=tool==TextEditorUI::DrawTool::Arrow||tool==TextEditorUI::DrawTool::Rectangle||tool==TextEditorUI::DrawTool::Ellipse;
        Find<controls::Button>(L"Shapes").Background(shape?activeBrush:idleBrush);
        Find<controls::Button>(L"Shapes").Content(winrt::box_value(tool==TextEditorUI::DrawTool::Arrow?L"↗  ▾":tool==TextEditorUI::DrawTool::Ellipse?L"○  ▾":L"□  ▾"));
        auto color=editor.AnnotationColor();
        Find<controls::Border>(L"ColorSwatch").Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,BYTE(color>>16),BYTE(color>>8),BYTE(color)}));
        Find<controls::ComboBox>(L"InkWidth").SelectedIndex(editor.AnnotationSize());
        Find<controls::Button>(L"Undo").IsEnabled(editor.CanUndo());Find<controls::Button>(L"Redo").IsEnabled(editor.CanRedo());
        RefreshCards();syncing=false;RefreshComposer();
    }
    bool Key(const MSG& message){
        if(!CloudAccessAllowed())return false;
        if(message.message!=WM_KEYDOWN)return false;
        if(editor.Interacting()&&GetFocus()==window){
            if(message.wParam==VK_ESCAPE){editor.SetInteract(false);return true;}
            if((GetKeyState(VK_CONTROL)&0x8000)&&(message.wParam=='S'||message.wParam=='O'))return editor.Key(message);
            return false;
        }
        if(message.wParam==VK_TAB&&GetFocus()==window){top.NavigateFocus(hosting::XamlSourceFocusNavigationRequest((GetKeyState(VK_SHIFT)&0x8000)?hosting::XamlSourceFocusNavigationReason::Last:hosting::XamlSourceFocusNavigationReason::First));return true;}
        // Let XAML text fields own ordinary keys and their local undo. File shortcuts
        // remain application-wide; stage shortcuts only run when the stage has focus.
        if(GetFocus()!=window&&!editor.IsEditing()){
            if((GetKeyState(VK_CONTROL)&0x8000)&&(message.wParam=='S'||message.wParam=='O'))return editor.Key(message);
            if((GetKeyState(VK_CONTROL)&0x8000)&&(message.wParam=='Z'||message.wParam=='Y')){
                auto root=bottom.HasFocus()?footer.XamlRoot():header.XamlRoot();
                auto focused=ux::Input::FocusManager::GetFocusedElement(root);
                if(!focused.try_as<controls::TextBox>()&&!focused.try_as<controls::RichEditBox>())return editor.Key(message);
            }
            return false;
        }
        return editor.Key(message);
    }
};
}
