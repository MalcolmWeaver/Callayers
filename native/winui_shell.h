#pragma once
#include "text_editor_ui.h"
#include "ai_client.h"
#include <MddBootstrap.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <Microsoft.UI.Dispatching.Interop.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <dwmapi.h>

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
    std::shared_ptr<int> lifetime=std::make_shared<int>(0);
    ux::FrameworkElement header{nullptr},footer{nullptr};
    bool syncing=false,generating=false;
    AiJob job;
    std::optional<CardRequestTarget> lastTarget;
    std::wstring lastPrompt;
    template<class T> T Bottom(const wchar_t* name){return footer.FindName(name).as<T>();}
    void AiMessage(const std::wstring& message){Bottom<controls::TextBlock>(L"AiStatus").Text(message);}
    void RefreshComposer(){
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
            job.Start(LoadAiSettings(),*lastTarget,lastPrompt);generating=true;
            Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Collapsed);
            AiMessage(!WebsiteUrl(lastPrompt).empty()?L"Reading the website and creating your card…":lastTarget->creating?L"Creating your card…":L"Updating “"+lastTarget->card.name+L"”… Your current card stays visible.");RefreshComposer();
        }catch(const std::exception& e){AiMessage(std::wstring(winrt::to_hstring(e.what())));Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Visible);}
        catch(...){AiMessage(L"Could not start generation. Check AI settings and retry.");}
    }
    void PollGeneration(){
        if(!generating)return;auto result=job.TakeResult();if(!result)return;
        generating=false;
        if(result->card){
            if(lastTarget&&editor.AcceptGenerated(*lastTarget,result->card->html,result->card->name)){AiMessage(L"Card ready. You can move it, resize it, or undo the change.");if(Bottom<controls::TextBox>(L"Prompt").Text()==lastPrompt)Bottom<controls::TextBox>(L"Prompt").Text(L"");}
            else AiMessage(L"The original card or scene changed. The late result was not applied.");
        }else{AiMessage(result->error);Bottom<controls::Button>(L"RetryGeneration").Visibility(ux::Visibility::Visible);}
        RefreshComposer();
    }
    void ShowAiSettings(){
        if(settingsIsland)return;
        auto settings=LoadAiSettings();settingsIsland=hosting::DesktopWindowXamlSource();settingsIsland.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        controls::Grid root;root.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,28,28,32}));settingsIsland.Content(root);
        RECT bounds;GetClientRect(window,&bounds);settingsIsland.SiteBridge().MoveAndResize({0,0,bounds.right,bounds.bottom});
        SetWindowPos(winrt::Microsoft::UI::GetWindowFromWindowId(settingsIsland.SiteBridge().WindowId()),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        controls::ContentDialog dialog;settingsDialog=dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(ux::ElementTheme::Dark);
        dialog.Title(winrt::box_value(L"OpenAI settings"));dialog.PrimaryButtonText(L"Save");dialog.CloseButtonText(L"Cancel");
        controls::StackPanel panel;panel.Spacing(12);panel.MinWidth(400);
        controls::TextBlock description;description.Text(L"Your prompt, any website text retrieved from a URL in it, and the card being revised are sent to OpenAI. Webcam frames and other cards are not sent.");description.TextWrapping(ux::TextWrapping::Wrap);panel.Children().Append(description);
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

    template<class T> T Find(const wchar_t* name){return header.FindName(name).as<T>();}
    void Action(const wchar_t* name,std::function<void()> action){
        Find<controls::Button>(name).Click([this,action](auto&&,auto&&){try{action();}catch(...){EditorError(window);}});
    }
    void CommitSize(){
        if(syncing)return;auto number=Find<controls::NumberBox>(L"FontSize").Value();if(!std::isfinite(number))return;
        editor.ChangeStyle([number](auto& t){t.size=std::clamp(number,8.,300.);});
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
  <Style TargetType="Button"><Setter Property="CornerRadius" Value="6"/><Setter Property="Padding" Value="12,7"/><Setter Property="MinHeight" Value="34"/></Style>
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
   <Button x:Name="FileButton" Content="File"><Button.Flyout><MenuFlyout><MenuFlyoutItem x:Name="NewDeck" Text="New deck"/><MenuFlyoutItem x:Name="OpenDeck" Text="Open…"/><MenuFlyoutSeparator/><MenuFlyoutItem x:Name="SaveDeck" Text="Save"/><MenuFlyoutItem x:Name="SaveAsDeck" Text="Save as…"/></MenuFlyout></Button.Flyout></Button>
   <Button x:Name="Save" Content="Save" ToolTipService.ToolTip="Save deck (Ctrl+S)"/>
  </StackPanel>
  <StackPanel Grid.Column="3" Orientation="Horizontal" Spacing="8" VerticalAlignment="Center">
   <ComboBox x:Name="Camera" Width="188" MaxDropDownHeight="300" PlaceholderText="Camera" AutomationProperties.Name="Camera source"/>
   <Button x:Name="Output" Content="Start camera" Background="#A39BFA" Foreground="#201D33" BorderThickness="0" MinWidth="118" ToolTipService.ToolTip="Make Callayers available as a camera in your meeting app"/>
  </StackPanel>
 </Grid>
 <Border Grid.Row="1" BorderBrush="#39383F" BorderThickness="0,1,0,1" Padding="24,0">
  <ScrollViewer HorizontalScrollBarVisibility="Auto" VerticalScrollBarVisibility="Disabled" HorizontalScrollMode="Enabled">
   <StackPanel Orientation="Horizontal" Spacing="7" VerticalAlignment="Center">
    <Button x:Name="Select" Content="↖  Select" ToolTipService.ToolTip="Select and move elements"/>
    <Button x:Name="Text" Content="T  Text" ToolTipService.ToolTip="Place text (T)"/>
    <Button x:Name="Card" Content="+  Card" ToolTipService.ToolTip="Create a new card with AI"/>
    <Button x:Name="Image" Content="Image" ToolTipService.ToolTip="Import a local image; paste with Ctrl+V on the stage"/>
    <Button x:Name="Draw" Content="Draw" ToolTipService.ToolTip="Pen (P), eraser (E), or laser (L)"><Button.Flyout><MenuFlyout><MenuFlyoutItem x:Name="PenTool" Text="Pen"/><MenuFlyoutItem x:Name="EraserTool" Text="Eraser — whole strokes"/><MenuFlyoutItem x:Name="LaserTool" Text="Laser — fades away"/></MenuFlyout></Button.Flyout></Button>
    <Button x:Name="DeleteCard" Content="Delete card" Visibility="Collapsed"/>
    <Button x:Name="DeleteImage" Content="Delete image" Visibility="Collapsed"/>
    <Border Width="1" Height="23" Background="#49474F" Margin="8,0"/>
    <Button x:Name="Undo" Content="↶" ToolTipService.ToolTip="Undo (Ctrl+Z)" AutomationProperties.Name="Undo"/>
    <Button x:Name="Redo" Content="↷" ToolTipService.ToolTip="Redo (Ctrl+Y)" AutomationProperties.Name="Redo"/>
    <TextBlock x:Name="ToolHint" Text="Choose Text, then click the stage" Foreground="#AAA8B2" VerticalAlignment="Center" Margin="14,0"/>
    <StackPanel x:Name="LayerProperties" Orientation="Horizontal" Spacing="7" Visibility="Collapsed">
     <Button x:Name="BringFront" Content="To front" ToolTipService.ToolTip="Bring in front of text and images"/>
     <Button x:Name="SendBack" Content="To back" ToolTipService.ToolTip="Send behind text and images; above cards"/>
    </StackPanel>
    <StackPanel x:Name="InkProperties" Orientation="Horizontal" Spacing="7" Visibility="Collapsed">
     <ComboBox x:Name="InkColor" Width="128" AutomationProperties.Name="Pen color"><x:String>Mint</x:String><x:String>White</x:String><x:String>Periwinkle</x:String><x:String>Coral</x:String><x:String>Yellow</x:String></ComboBox>
     <ComboBox x:Name="InkWidth" Width="104" AutomationProperties.Name="Pen thickness"><x:String>Fine</x:String><x:String>Medium</x:String><x:String>Bold</x:String></ComboBox>
    </StackPanel>
    <StackPanel x:Name="TextProperties" Orientation="Horizontal" Spacing="7" Visibility="Collapsed">
     <Border Width="1" Height="23" Background="#49474F" Margin="8,0"/>
     <ComboBox x:Name="Font" Width="142" AutomationProperties.Name="Font family"><x:String>Segoe UI</x:String><x:String>Arial</x:String><x:String>Georgia</x:String><x:String>Consolas</x:String></ComboBox>
     <NumberBox x:Name="FontSize" Width="80" Minimum="8" Maximum="300" SmallChange="1" LargeChange="8" SpinButtonPlacementMode="Hidden" AutomationProperties.Name="Text size"/>
     <ToggleButton x:Name="Bold" Content="B" FontWeight="Bold" Width="36" AutomationProperties.Name="Bold"/>
     <Button x:Name="Color" Content="Color" ToolTipService.ToolTip="Text color"/>
     <ComboBox x:Name="Alignment" Width="104" AutomationProperties.Name="Text alignment"><x:String>Left</x:String><x:String>Center</x:String><x:String>Right</x:String></ComboBox>
     <Button x:Name="Delete" Content="Delete" ToolTipService.ToolTip="Delete selected text"/>
    </StackPanel>
   </StackPanel>
  </ScrollViewer>
 </Border>
</Grid>)XAML").as<ux::FrameworkElement>();
        footer=ux::Markup::XamlReader::Load(LR"XAML(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RequestedTheme="Dark" Background="#242428" Padding="24,10,24,0" RowSpacing="5">
 <Grid.RowDefinitions><RowDefinition Height="28"/><RowDefinition Height="40"/><RowDefinition Height="24"/><RowDefinition Height="28"/></Grid.RowDefinitions>
 <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
  <TextBlock x:Name="PromptTarget" Text="New card" FontWeight="SemiBold" VerticalAlignment="Center" TextTrimming="CharacterEllipsis"/>
  <Button x:Name="NewCard" Grid.Column="1" Content="New card" Padding="10,2" Margin="0,0,8,0"/>
  <Button x:Name="AiSettings" Grid.Column="2" Content="AI settings" Padding="10,2"/>
 </Grid>
 <Grid Grid.Row="1" ColumnSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
  <TextBox x:Name="Prompt" PlaceholderText="Describe a card, or summarize an https:// page…" MaxLength="4000" AutomationProperties.Name="Card prompt"/>
  <Button x:Name="Generate" Grid.Column="1" Content="Generate card" MinWidth="120" Background="#A39BFA" Foreground="#201D33"/>
  <Button x:Name="CancelGeneration" Grid.Column="2" Content="Cancel" Visibility="Collapsed"/>
 </Grid>
 <Grid Grid.Row="2" ColumnSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
  <ProgressRing x:Name="AiProgress" Width="18" Height="18" IsActive="False" Visibility="Collapsed"/>
  <TextBlock x:Name="AiStatus" Grid.Column="1" Text="Transparent cards, readable text. Select a card to revise it." FontSize="12" Foreground="#C4C0D2" VerticalAlignment="Center" TextTrimming="CharacterEllipsis"/>
  <Button x:Name="RetryGeneration" Grid.Column="2" Content="Retry" Padding="8,0" Visibility="Collapsed"/>
 </Grid>
 <TextBlock x:Name="Status" Grid.Row="3" Text="Camera output is off" FontSize="11" Foreground="#9692A4" VerticalAlignment="Center" TextTrimming="CharacterEllipsis"/>
</Grid>)XAML").as<ux::FrameworkElement>();
        top.Content(header);bottom.Content(footer);
        top.TakeFocusRequested([this](auto&&,auto&&){SetFocus(window);});
        bottom.TakeFocusRequested([this](auto&&,auto&&){SetFocus(window);});
        for(auto pair:{std::pair{L"Select",TextEditorUI::SelectId},{L"Text",TextEditorUI::TextId},{L"Undo",TextEditorUI::UndoId},{L"Redo",TextEditorUI::RedoId},{L"Delete",TextEditorUI::DeleteId},{L"Save",TextEditorUI::SaveId}})
            Action(pair.first,[this,id=pair.second]{editor.Command(id);});
        for(auto pair:{std::pair{L"NewDeck",TextEditorUI::NewIdControl},{L"OpenDeck",TextEditorUI::OpenId},{L"SaveDeck",TextEditorUI::SaveId}})
            Find<controls::MenuFlyoutItem>(pair.first).Click([this,id=pair.second](auto&&,auto&&){try{editor.Command(id);}catch(...){EditorError(window);}});
        Find<controls::MenuFlyoutItem>(L"SaveAsDeck").Click([this](auto&&,auto&&){try{editor.SaveAs();}catch(...){EditorError(window);}});
        auto sources=Find<controls::ComboBox>(L"Camera");sources.Items().Append(winrt::box_value(L"Test pattern"));
        for(auto& device:devices)sources.Items().Append(winrt::box_value(device.name));sources.SelectedIndex(0);
        sources.SelectionChanged([this,camera](auto&&,auto&&){if(!syncing)try{camera(Find<controls::ComboBox>(L"Camera").SelectedIndex());}catch(...){EditorError(window);}});
        Action(L"Card",[this]{editor.NewCardSelection();Bottom<controls::TextBox>(L"Prompt").Focus(ux::FocusState::Programmatic);});Action(L"DeleteCard",[this]{editor.Command(TextEditorUI::DeleteId);});
        Action(L"Output",output);Action(L"Color",[this]{editor.ChooseTextColor();});
        Action(L"Image",[this]{editor.ChooseImage();});Action(L"DeleteImage",[this]{editor.Command(TextEditorUI::DeleteId);});
        Action(L"BringFront",[this]{editor.OrderForeground(true);});Action(L"SendBack",[this]{editor.OrderForeground(false);});
        for(auto pair:{std::pair{L"PenTool",TextEditorUI::DrawTool::Pen},{L"EraserTool",TextEditorUI::DrawTool::Eraser},{L"LaserTool",TextEditorUI::DrawTool::Laser}})
            Find<controls::MenuFlyoutItem>(pair.first).Click([this,tool=pair.second](auto&&,auto&&){try{editor.SetDrawingTool(tool);}catch(...){EditorError(window);}});
        Find<controls::ComboBox>(L"InkColor").SelectionChanged([this](auto&&,auto&&){if(syncing)return;constexpr uint32_t colors[]{0xbaf2cd,0xffffff,0xa39bfa,0xff647c,0xffdc64};auto index=Find<controls::ComboBox>(L"InkColor").SelectedIndex();if(index>=0&&index<5)editor.SetPenColor(colors[index]);});
        Find<controls::ComboBox>(L"InkWidth").SelectionChanged([this](auto&&,auto&&){if(syncing)return;auto index=Find<controls::ComboBox>(L"InkWidth").SelectedIndex();if(index>=0)editor.SetPenWidth(index==0?3:index==1?6:12);});
        Find<controls::ComboBox>(L"Font").SelectionChanged([this](auto&&,auto&&){if(syncing)return;try{auto value=winrt::unbox_value<winrt::hstring>(Find<controls::ComboBox>(L"Font").SelectedItem());editor.ChangeStyle([&](auto& t){t.family=value;});}catch(...){EditorError(window);}});
        Find<controls::ComboBox>(L"Alignment").SelectionChanged([this](auto&&,auto&&){if(syncing)return;try{auto index=Find<controls::ComboBox>(L"Alignment").SelectedIndex();editor.ChangeStyle([index](auto& t){t.alignment=index;});}catch(...){EditorError(window);}});
        Find<controls::Primitives::ToggleButton>(L"Bold").Click([this](auto&&,auto&&){if(syncing)return;try{auto bold=Find<controls::Primitives::ToggleButton>(L"Bold").IsChecked().Value();editor.ChangeStyle([bold](auto& t){t.bold=bold;});}catch(...){EditorError(window);}});
        Find<controls::NumberBox>(L"FontSize").ValueChanged([this](auto&&,auto&&){try{CommitSize();}catch(...){EditorError(window);}});
        Bottom<controls::Button>(L"NewCard").Click([this](auto&&,auto&&){editor.NewCardSelection();Bottom<controls::TextBox>(L"Prompt").Focus(ux::FocusState::Programmatic);});
        Bottom<controls::Button>(L"Generate").Click([this](auto&&,auto&&){Generate();});
        Bottom<controls::Button>(L"RetryGeneration").Click([this](auto&&,auto&&){Generate(true);});
        Bottom<controls::Button>(L"CancelGeneration").Click([this](auto&&,auto&&){job.Cancel();generating=false;AiMessage(L"Canceled. Your cards are unchanged.");RefreshComposer();});
        Bottom<controls::Button>(L"AiSettings").Click([this](auto&&,auto&&){try{ShowAiSettings();}catch(...){EditorError(window);}});
        Bottom<controls::TextBox>(L"Prompt").KeyDown([this](auto&&,ux::Input::KeyRoutedEventArgs const& args){if(args.Key()==winrt::Windows::System::VirtualKey::Enter){Generate();args.Handled(true);}});
        editor.StateChanged=[this]{Refresh();};Refresh();
        BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    }
    ~WinUIShell(){lifetime.reset();job.Cancel();editor.StateChanged={};if(settingsDialog)settingsDialog.Hide();if(settingsIsland)settingsIsland.Close();if(top)top.Close();if(bottom)bottom.Close();}
    void Layout(int width,int height,double scale){
        if(settingsIsland)settingsIsland.SiteBridge().MoveAndResize({0,0,width,height});
        int topHeight=static_cast<int>(128*scale),bottomHeight=static_cast<int>(150*scale);
        top.SiteBridge().MoveAndResize({0,0,width,topHeight});bottom.SiteBridge().MoveAndResize({0,std::max(topHeight,height-bottomHeight),width,bottomHeight});
    }
    void CameraIndex(int index){syncing=true;Find<controls::ComboBox>(L"Camera").SelectedIndex(index);syncing=false;}
    void Status(const std::wstring& message,bool busy,bool running){
        PollGeneration();
        auto status=footer.FindName(L"Status").as<controls::TextBlock>();if(status.Text()!=message)status.Text(message);
        auto button=Find<controls::Button>(L"Output");if(button.IsEnabled()==busy)button.IsEnabled(!busy);
        winrt::hstring label=busy?L"Starting…":running?L"Stop camera":L"Start camera";
        if(winrt::unbox_value<winrt::hstring>(button.Content())!=label)button.Content(winrt::box_value(label));
        auto name=Find<controls::TextBlock>(L"DeckName");if(name.Text()!=editor.Name())name.Text(editor.Name());
        auto save=Find<controls::TextBlock>(L"SaveState");winrt::hstring saved=editor.Dirty()?L"Unsaved changes":L"No unsaved changes";if(save.Text()!=saved)save.Text(saved);
    }
    void Refresh(){
        syncing=true;auto selection=editor.Selection();
        auto tool=editor.DrawingTool();bool drawing=tool!=TextEditorUI::DrawTool::None,pen=tool==TextEditorUI::DrawTool::Pen;
        Find<controls::Button>(L"Draw").Content(winrt::box_value(pen?L"Pen":tool==TextEditorUI::DrawTool::Eraser?L"Eraser":tool==TextEditorUI::DrawTool::Laser?L"Laser":L"Draw"));
        Find<controls::Button>(L"Draw").Background(ux::Media::SolidColorBrush(drawing?winrt::Windows::UI::Color{255,76,69,116}:winrt::Windows::UI::Color{255,48,47,53}));
        Find<controls::StackPanel>(L"InkProperties").Visibility(pen?ux::Visibility::Visible:ux::Visibility::Collapsed);
        constexpr uint32_t colors[]{0xbaf2cd,0xffffff,0xa39bfa,0xff647c,0xffdc64};for(int i=0;i<5;++i)if(editor.PenColor()==colors[i])Find<controls::ComboBox>(L"InkColor").SelectedIndex(i);
        Find<controls::ComboBox>(L"InkWidth").SelectedIndex(editor.PenWidth()<=3?0:editor.PenWidth()<=6?1:2);
        Find<controls::Button>(L"DeleteCard").Visibility(editor.CardSelected()?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Find<controls::Button>(L"DeleteImage").Visibility(editor.ImageSelected()?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Find<controls::StackPanel>(L"LayerProperties").Visibility(selection||editor.ImageSelected()?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Find<controls::StackPanel>(L"TextProperties").Visibility(selection?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Find<controls::TextBlock>(L"ToolHint").Visibility(selection||pen||editor.ImageSelected()?ux::Visibility::Collapsed:ux::Visibility::Visible);
        Find<controls::TextBlock>(L"ToolHint").Text(tool==TextEditorUI::DrawTool::Eraser?L"Drag across strokes to erase":tool==TextEditorUI::DrawTool::Laser?L"Drag to point; trails fade":editor.TextTool()?L"Click the stage to place text":editor.CardSelected()?L"Drag to move; handles resize":L"Add text, cards, or drawing");
        Find<controls::Button>(L"Undo").IsEnabled(editor.CanUndo());Find<controls::Button>(L"Redo").IsEnabled(editor.CanRedo());
        for(auto name:{L"Text",L"Select"}){
            bool active=!drawing&&editor.TextTool()==(std::wstring_view(name)==L"Text");
            Find<controls::Button>(name).Background(ux::Media::SolidColorBrush(active?winrt::Windows::UI::Color{255,76,69,116}:winrt::Windows::UI::Color{255,48,47,53}));
        }
        if(selection){auto& t=*selection;
            Find<controls::ComboBox>(L"Font").SelectedItem(winrt::box_value(t.family));Find<controls::NumberBox>(L"FontSize").Value(t.size);
            Find<controls::Primitives::ToggleButton>(L"Bold").IsChecked(t.bold);Find<controls::ComboBox>(L"Alignment").SelectedIndex(t.alignment);
        }
        syncing=false;RefreshComposer();
    }
    bool Key(const MSG& message){
        if(message.message!=WM_KEYDOWN)return false;
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
