#pragma once
#include "text_editor_ui.h"
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
    hosting::DesktopWindowXamlSource top{nullptr},bottom{nullptr};
    ux::FrameworkElement header{nullptr},footer{nullptr};
    bool syncing=false;
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
   <Viewbox Width="31" Height="31"><Canvas Width="100" Height="100"><Rectangle Width="100" Height="100" RadiusX="20" RadiusY="20" Fill="#5E78F5"/><Path Data="M 60 18 C 63.3 18 66 20.7 66 24 L 66 27 C 66 28.7 64.7 30 63 30 L 30 30 C 28.3 30 27 31.3 27 33 L 27 54 C 27 55.7 28.3 57 30 57 L 40 57 L 40 69 L 24 69 C 19 69 15 65 15 60 L 15 27 C 15 22 19 18 24 18 Z" Fill="White"/><Path Data="M 77 35 C 80.3 35 83 37.7 83 41 L 83 44 C 83 45.7 81.7 47 80 47 L 47 47 C 45.3 47 44 48.3 44 50 L 44 71 C 44 72.7 45.3 74 47 74 L 80 74 C 81.7 74 83 75.3 83 77 L 83 80 C 83 83.3 80.3 86 77 86 L 41 86 C 36 86 32 82 32 77 L 32 44 C 32 39 36 35 41 35 Z" Fill="White"/></Canvas></Viewbox>
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
    <Button x:Name="Select" Content="↖  Select" ToolTipService.ToolTip="Select and move text"/>
    <Button x:Name="Text" Content="T  Text" ToolTipService.ToolTip="Place text (T)"/>
    <Border Width="1" Height="23" Background="#49474F" Margin="8,0"/>
    <Button x:Name="Undo" Content="↶" ToolTipService.ToolTip="Undo (Ctrl+Z)" AutomationProperties.Name="Undo"/>
    <Button x:Name="Redo" Content="↷" ToolTipService.ToolTip="Redo (Ctrl+Y)" AutomationProperties.Name="Redo"/>
    <TextBlock x:Name="ToolHint" Text="Choose Text, then click the stage" Foreground="#AAA8B2" VerticalAlignment="Center" Margin="14,0"/>
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
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RequestedTheme="Dark" Background="#242428" Padding="24,0">
 <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
 <TextBlock x:Name="Status" Text="Camera output is off" FontSize="12" Foreground="#B9B7C4" VerticalAlignment="Center" TextTrimming="CharacterEllipsis"/>
 <TextBlock Grid.Column="1" Text="Double-click to edit  ·  Ctrl+Enter to finish" FontSize="12" Foreground="#8E8B99" VerticalAlignment="Center" Margin="24,0,0,0"/>
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
        Action(L"Output",output);Action(L"Color",[this]{editor.ChooseTextColor();});
        Find<controls::ComboBox>(L"Font").SelectionChanged([this](auto&&,auto&&){if(syncing)return;try{auto value=winrt::unbox_value<winrt::hstring>(Find<controls::ComboBox>(L"Font").SelectedItem());editor.ChangeStyle([&](auto& t){t.family=value;});}catch(...){EditorError(window);}});
        Find<controls::ComboBox>(L"Alignment").SelectionChanged([this](auto&&,auto&&){if(syncing)return;try{auto index=Find<controls::ComboBox>(L"Alignment").SelectedIndex();editor.ChangeStyle([index](auto& t){t.alignment=index;});}catch(...){EditorError(window);}});
        Find<controls::Primitives::ToggleButton>(L"Bold").Click([this](auto&&,auto&&){if(syncing)return;try{auto bold=Find<controls::Primitives::ToggleButton>(L"Bold").IsChecked().Value();editor.ChangeStyle([bold](auto& t){t.bold=bold;});}catch(...){EditorError(window);}});
        Find<controls::NumberBox>(L"FontSize").ValueChanged([this](auto&&,auto&&){try{CommitSize();}catch(...){EditorError(window);}});
        editor.StateChanged=[this]{Refresh();};Refresh();
        BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    }
    ~WinUIShell(){editor.StateChanged={};if(top)top.Close();if(bottom)bottom.Close();}
    void Layout(int width,int height,double scale){
        int topHeight=static_cast<int>(128*scale),bottomHeight=static_cast<int>(38*scale);
        top.SiteBridge().MoveAndResize({0,0,width,topHeight});bottom.SiteBridge().MoveAndResize({0,std::max(topHeight,height-bottomHeight),width,bottomHeight});
    }
    void CameraIndex(int index){syncing=true;Find<controls::ComboBox>(L"Camera").SelectedIndex(index);syncing=false;}
    void Status(const std::wstring& message,bool busy,bool running){
        auto status=footer.FindName(L"Status").as<controls::TextBlock>();if(status.Text()!=message)status.Text(message);
        auto button=Find<controls::Button>(L"Output");if(button.IsEnabled()==busy)button.IsEnabled(!busy);
        winrt::hstring label=busy?L"Starting…":running?L"Stop camera":L"Start camera";
        if(winrt::unbox_value<winrt::hstring>(button.Content())!=label)button.Content(winrt::box_value(label));
        auto name=Find<controls::TextBlock>(L"DeckName");if(name.Text()!=editor.Name())name.Text(editor.Name());
        auto save=Find<controls::TextBlock>(L"SaveState");winrt::hstring saved=editor.Dirty()?L"Unsaved changes":L"No unsaved changes";if(save.Text()!=saved)save.Text(saved);
    }
    void Refresh(){
        syncing=true;auto selection=editor.Selection();
        Find<controls::StackPanel>(L"TextProperties").Visibility(selection?ux::Visibility::Visible:ux::Visibility::Collapsed);
        Find<controls::TextBlock>(L"ToolHint").Visibility(selection?ux::Visibility::Collapsed:ux::Visibility::Visible);
        Find<controls::TextBlock>(L"ToolHint").Text(editor.TextTool()?L"Click the stage to place text":L"Choose Text, then click the stage");
        Find<controls::Button>(L"Undo").IsEnabled(editor.CanUndo());Find<controls::Button>(L"Redo").IsEnabled(editor.CanRedo());
        for(auto name:{L"Text",L"Select"}){
            bool active=editor.TextTool()==(std::wstring_view(name)==L"Text");
            Find<controls::Button>(name).Background(ux::Media::SolidColorBrush(active?winrt::Windows::UI::Color{255,76,69,116}:winrt::Windows::UI::Color{255,48,47,53}));
        }
        if(selection){auto& t=*selection;
            Find<controls::ComboBox>(L"Font").SelectedItem(winrt::box_value(t.family));Find<controls::NumberBox>(L"FontSize").Value(t.size);
            Find<controls::Primitives::ToggleButton>(L"Bold").IsChecked(t.bold);Find<controls::ComboBox>(L"Alignment").SelectedIndex(t.alignment);
        }
        syncing=false;
    }
    bool Key(const MSG& message){
        if(message.message!=WM_KEYDOWN)return false;
        if(message.wParam==VK_TAB&&GetFocus()==window){top.NavigateFocus(hosting::XamlSourceFocusNavigationRequest((GetKeyState(VK_SHIFT)&0x8000)?hosting::XamlSourceFocusNavigationReason::Last:hosting::XamlSourceFocusNavigationReason::First));return true;}
        // Let XAML text fields own ordinary keys and their local undo. File shortcuts
        // remain application-wide; stage shortcuts only run when the stage has focus.
        if(GetFocus()!=window&&!editor.IsEditing()){
            if((GetKeyState(VK_CONTROL)&0x8000)&&(message.wParam=='S'||message.wParam=='O'))return editor.Key(message);
            if((GetKeyState(VK_CONTROL)&0x8000)&&(message.wParam=='Z'||message.wParam=='Y')){
                auto focused=ux::Input::FocusManager::GetFocusedElement(header.XamlRoot());
                if(!focused.try_as<controls::TextBox>()&&!focused.try_as<controls::RichEditBox>())return editor.Key(message);
            }
            return false;
        }
        return editor.Key(message);
    }
};
}
