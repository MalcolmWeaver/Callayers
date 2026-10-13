#include "workspace.h"
#include "resource_diagnostics.h"
#include "text_editor.h"
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
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
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <cwchar>

namespace callayers {
namespace ux = winrt::Microsoft::UI::Xaml;
namespace controls = ux::Controls;
namespace hosting = ux::Hosting;
static constexpr const wchar_t* WorkspacePreferenceKey=L"Software\\Callayers\\WorkspaceV2";
static bool LoadAnnotationBarOpen() {
    DWORD open=1,bytes=sizeof(open);
    LSTATUS result=RegGetValueW(HKEY_CURRENT_USER,WorkspacePreferenceKey,L"AnnotateBarOpen",RRF_RT_REG_DWORD,nullptr,&open,&bytes);
    if(result==ERROR_SUCCESS && bytes==sizeof(open) && open<=1)return open!=0;
    if(result!=ERROR_FILE_NOT_FOUND && result!=ERROR_PATH_NOT_FOUND)Resources().event("annotation_bar_preference_read_failed");
    return true;
}
static void SaveAnnotationBarOpen(bool open) {
    HKEY key=nullptr;
    LSTATUS result=RegCreateKeyExW(HKEY_CURRENT_USER,WorkspacePreferenceKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(result==ERROR_SUCCESS) {
        DWORD value=open?1:0;
        result=RegSetValueExW(key,L"AnnotateBarOpen",0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
        RegCloseKey(key);
    }
    if(result!=ERROR_SUCCESS)Resources().event("annotation_bar_preference_write_failed");
}
// Only startup/shutdown checkpoints use synchronous writes. They must survive
// a framework crash before the background resource logger's first interval.
static void StartupTrace(const char* stage, bool reset=false) {
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr,exe,32768);
    auto directory=std::filesystem::path(exe).parent_path()/L"diagnostics";
    std::error_code error;std::filesystem::create_directories(directory,error);
    std::ofstream file(directory/L"workspace-startup.log",reset?std::ios::trunc:std::ios::app);
    file<<GetTickCount64()<<' '<<stage<<" build="<<__DATE__<<' '<<__TIME__<<std::endl;
}
struct WorkspaceApplication : ux::ApplicationT<WorkspaceApplication, ux::Markup::IXamlMetadataProvider> {
    ux::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
    ux::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type) { return provider.GetXamlType(type); }
    ux::Markup::IXamlType GetXamlType(winrt::hstring const& name) { return provider.GetXamlType(name); }
    winrt::com_array<ux::Markup::XmlnsDefinition> GetXmlnsDefinitions() { return provider.GetXmlnsDefinitions(); }
};
struct WorkspaceRuntime::Impl {
    bool bootstrapped = false;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueController queue{nullptr};
    ux::Application application{nullptr};
    hosting::WindowsXamlManager manager{nullptr};
    Impl() {
        StartupTrace("runtime-start",true);
        Resources().event("workspace_runtime_start");
        PACKAGE_VERSION minimum{};
        winrt::check_hresult(MddBootstrapInitialize2(0x00010006, L"", minimum, MddBootstrapInitializeOptions_None));
        StartupTrace("bootstrap-ready");
        bootstrapped = true;
        try {
            queue = winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
            StartupTrace("dispatcher-ready");
            application = winrt::make<WorkspaceApplication>();
            StartupTrace("application-ready");
            manager = hosting::WindowsXamlManager::InitializeForCurrentThread();
            StartupTrace("xaml-ready");
            application.RequestedTheme(ux::ApplicationTheme::Dark);
            StartupTrace("theme-ready");
            application.Resources().MergedDictionaries().Append(controls::XamlControlsResources());
            StartupTrace("resources-ready");
            Resources().event("workspace_runtime_ready");
        } catch (...) { MddBootstrapShutdown(); bootstrapped = false; throw; }
    }
    ~Impl() {
        if (manager) manager.Close();
        manager = nullptr; application = nullptr;
        if (queue) {
            auto shutdown = queue.ShutdownQueueAsync();
            // Shutdown completes on this thread; pump its messages rather than
            // blocking on an async operation that needs the same dispatcher.
            while (shutdown.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
                MSG message{};
                if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
                } else MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        }
        queue = nullptr;
        if (bootstrapped) MddBootstrapShutdown();
    }
};
WorkspaceRuntime::WorkspaceRuntime() : impl_(std::make_unique<Impl>()) {}
WorkspaceRuntime::~WorkspaceRuntime() = default;

struct Workspace::Impl {
    HWND window;
    WorkspaceActions actions;
    hosting::DesktopWindowXamlSource headerIsland, sidebarIsland, toolbarIsland, helpIsland;
    ux::FrameworkElement header{nullptr}, sidebar{nullptr}, toolbar{nullptr}, help{nullptr};
    controls::ContentControl sidebarGate, toolbarGate;
    TextEditor editor;
    bool finishingText=false;
    unsigned editorToken=0;
    std::shared_ptr<int> lifetime=std::make_shared<int>(0);
    bool syncing = false, annotating = LoadAnnotationBarOpen();
    bool lastFresh = true, lastPrivacy = false;
    bool lastRetryPreference = false;
    bool listedVisible[3]{true,true,true};
    std::wstring lastStatus, lastHelp;
    int selected = -1;
    static constexpr const wchar_t* names[] = {L"Name card", L"Animated card", L"Five-minute timer"};
    template<class T> T find(ux::FrameworkElement const& root, const wchar_t* name) { return root.FindName(name).as<T>(); }
    static ux::FrameworkElement load(const wchar_t* xaml) { return ux::Markup::XamlReader::Load(xaml).as<ux::FrameworkElement>(); }
    void attach(hosting::DesktopWindowXamlSource const& island, ux::FrameworkElement const& content,bool visible=true) {
        island.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        content.RequestedTheme(ux::ElementTheme::Dark); island.Content(content);
        if(visible)island.SiteBridge().Show();else island.SiteBridge().Hide();
    }
    std::vector<hosting::DesktopWindowXamlSource> focusOrder() {
        if(!lastFresh)return {headerIsland,helpIsland};
        if(annotating)return {headerIsland,sidebarIsland,toolbarIsland};
        return {headerIsland,sidebarIsland};
    }
    void focusTarget(hosting::DesktopWindowXamlSource const& target,bool reverse) {
        target.NavigateFocus(hosting::XamlSourceFocusNavigationRequest(reverse?hosting::XamlSourceFocusNavigationReason::Last:hosting::XamlSourceFocusNavigationReason::First));
    }
    Impl(HWND parent, WorkspaceActions callbacks) : window(parent), actions(std::move(callbacks)) {
        StartupTrace("controls-start");
        header = load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#242428" Padding="20,0" ColumnSpacing="12">
 <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
 <StackPanel Orientation="Horizontal" Spacing="10" VerticalAlignment="Center"><Image x:Name="Logo" Width="32" Height="32" AutomationProperties.Name="Callayers logo"/><TextBlock Text="Callayers" FontSize="21" FontWeight="SemiBold" VerticalAlignment="Center" Margin="0,0,16,0"/></StackPanel>
 <Button Grid.Column="1" Content="File" VerticalAlignment="Center"><Button.Flyout><MenuFlyout><MenuFlyoutItem x:Name="Quit" Text="Quit"/></MenuFlyout></Button.Flyout></Button>
 <ToggleButton Grid.Column="2" x:Name="Annotate" Content="✎  Annotate" IsChecked="False" VerticalAlignment="Center"/>
 <ComboBox Grid.Column="3" x:Name="Camera" PlaceholderText="Choose your webcam" Width="220" HorizontalAlignment="Right" VerticalAlignment="Center" AutomationProperties.Name="Webcam"/>
 <StackPanel Grid.Column="4" Width="180" VerticalAlignment="Center" Spacing="2"><TextBlock x:Name="Status" FontSize="12" Foreground="#BBB9C8" TextWrapping="Wrap"/><Button x:Name="RetryChoice" Content="Retry" Visibility="Collapsed" ToolTipService.ToolTip="Try again"/></StackPanel>
</Grid>)");
        sidebar = load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#242428" Padding="20" RowSpacing="16">
 <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
 <StackPanel Spacing="12"><TextBlock Text="Cards" FontSize="22" FontWeight="SemiBold"/>
 <Button Content="+  Create with AI" HorizontalAlignment="Stretch" IsEnabled="False" ToolTipService.ToolTip="AI card creation is coming soon."/>
 <StackPanel Orientation="Horizontal" Spacing="8"><Button Content="Import image" IsEnabled="False" ToolTipService.ToolTip="Image import is coming soon."/><Button x:Name="HideAll" Content="Hide all"/></StackPanel>
 <TextBox x:Name="Search" PlaceholderText="Search cards" AutomationProperties.Name="Search cards"/>
 </StackPanel>
 <ScrollViewer Grid.Row="1" HorizontalScrollBarVisibility="Disabled" VerticalScrollBarVisibility="Auto"><StackPanel Spacing="12">
 <StackPanel x:Name="Rows" Spacing="6">
 <Border x:Name="Row0" CornerRadius="6" Padding="4"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><CheckBox x:Name="Show0" IsChecked="True" AutomationProperties.Name="Show Name card"/><Button x:Name="Name0" Grid.Column="1" Content="Name card" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left" Background="Transparent" BorderThickness="0"/></Grid></Border>
 <Border x:Name="Row1" CornerRadius="6" Padding="4"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><CheckBox x:Name="Show1" IsChecked="True" AutomationProperties.Name="Show Animated card"/><Button x:Name="Name1" Grid.Column="1" Content="Animated card" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left" Background="Transparent" BorderThickness="0"/></Grid></Border>
 <Border x:Name="Row2" CornerRadius="6" Padding="4"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><CheckBox x:Name="Show2" IsChecked="True" AutomationProperties.Name="Show Five-minute timer"/><Button x:Name="Name2" Grid.Column="1" Content="Five-minute timer" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left" Background="Transparent" BorderThickness="0"/></Grid></Border>
 </StackPanel>
 <TextBlock x:Name="NoResults" Text="No matching cards" Foreground="#BBB9C8" Visibility="Collapsed"/>
 <Border Background="#303036" CornerRadius="8" Padding="12"><StackPanel Spacing="8"><TextBlock x:Name="Selection" Text="Select a card" FontWeight="SemiBold" TextWrapping="Wrap"/><TextBlock x:Name="SelectionHelp" Text="Check a box to show a card. Select its name to work with it." Foreground="#BBB9C8" TextWrapping="Wrap" FontSize="12"/></StackPanel></Border>
 <TextBlock Text="Sample cards are temporary. Annotations save automatically." FontSize="12" Foreground="#BBB9C8" TextWrapping="Wrap"/>
 </StackPanel></ScrollViewer>
 <StackPanel Grid.Row="2" Spacing="8"><Button x:Name="UseInCall" Content="Use in a call" HorizontalAlignment="Stretch"><Button.Flyout><Flyout><TextBlock Text="In your calling app, choose Callayers as your camera." TextWrapping="Wrap" Width="240"/></Flyout></Button.Flyout></Button><TextBlock Text="In your calling app, choose Callayers" FontSize="12" Foreground="#BBB9C8" TextWrapping="Wrap"/></StackPanel>
</Grid>)");
        toolbar = load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#1D1D21" Padding="16,0"><StackPanel Orientation="Horizontal" Spacing="6" VerticalAlignment="Center">
 <ToggleButton x:Name="Select" Content="↖" MinWidth="40" ToolTipService.ToolTip="Select and move or resize" AutomationProperties.Name="Select" IsChecked="True"/>
 <ToggleButton x:Name="Pen" Content="✎" MinWidth="40" ToolTipService.ToolTip="Pen" AutomationProperties.Name="Pen"/>
 <DropDownButton x:Name="Shapes" MinWidth="62" ToolTipService.ToolTip="Shapes" AutomationProperties.Name="Shapes"><Viewbox Width="22" Height="22"><Grid Width="24" Height="24"><Rectangle Width="12" Height="12" HorizontalAlignment="Left" VerticalAlignment="Top" Margin="2,2,0,0" Stroke="#E6EBF5" StrokeThickness="1.7" RadiusX="1" RadiusY="1"/><Ellipse Width="12" Height="12" HorizontalAlignment="Left" VerticalAlignment="Top" Margin="10,10,0,0" Stroke="#E6EBF5" StrokeThickness="1.7"/></Grid></Viewbox><DropDownButton.Flyout><MenuFlyout>
 <MenuFlyoutItem x:Name="Rectangle" Text="Rectangle"/><MenuFlyoutItem x:Name="Ellipse" Text="Ellipse"/><MenuFlyoutItem x:Name="Line" Text="Line"/><MenuFlyoutItem x:Name="Arrow" Text="Arrow"/>
 </MenuFlyout></DropDownButton.Flyout></DropDownButton>
 <ToggleButton x:Name="Text" Content="T" MinWidth="40" ToolTipService.ToolTip="Text; double-click existing text to edit" AutomationProperties.Name="Text"/>
 <Button x:Name="Color" ToolTipService.ToolTip="Annotation color" AutomationProperties.Name="Annotation color"><Border x:Name="ColorSwatch" Width="22" Height="22" Background="#FFD166" CornerRadius="3" BorderBrush="#B0B0B0" BorderThickness="1"/><Button.Flyout><Flyout><StackPanel Spacing="8">
 <TextBlock Text="Annotation color" FontWeight="SemiBold"/>
 <StackPanel Orientation="Horizontal" Spacing="6"><Button x:Name="Color0" Background="#000000" Width="32" Height="32" BorderBrush="#999999" ToolTipService.ToolTip="Black" AutomationProperties.Name="Black"/><Button x:Name="Color1" Background="#808080" Width="32" Height="32" ToolTipService.ToolTip="Gray" AutomationProperties.Name="Gray"/><Button x:Name="Color2" Background="#FFFFFF" Width="32" Height="32" ToolTipService.ToolTip="White" AutomationProperties.Name="White"/><Button x:Name="Color3" Background="#FF0000" Width="32" Height="32" ToolTipService.ToolTip="Red" AutomationProperties.Name="Red"/><Button x:Name="Color4" Background="#FF8000" Width="32" Height="32" ToolTipService.ToolTip="Orange" AutomationProperties.Name="Orange"/><Button x:Name="Color5" Background="#FFD166" Width="32" Height="32" ToolTipService.ToolTip="Yellow" AutomationProperties.Name="Yellow"/></StackPanel>
 <StackPanel Orientation="Horizontal" Spacing="6"><Button x:Name="Color6" Background="#00AA00" Width="32" Height="32" ToolTipService.ToolTip="Green" AutomationProperties.Name="Green"/><Button x:Name="Color7" Background="#00BFFF" Width="32" Height="32" ToolTipService.ToolTip="Cyan" AutomationProperties.Name="Cyan"/><Button x:Name="Color8" Background="#0000FF" Width="32" Height="32" ToolTipService.ToolTip="Blue" AutomationProperties.Name="Blue"/><Button x:Name="Color9" Background="#8000FF" Width="32" Height="32" ToolTipService.ToolTip="Purple" AutomationProperties.Name="Purple"/><Button x:Name="Color10" Background="#FF00FF" Width="32" Height="32" ToolTipService.ToolTip="Pink" AutomationProperties.Name="Pink"/><Button x:Name="Color11" Background="#804000" Width="32" Height="32" ToolTipService.ToolTip="Brown" AutomationProperties.Name="Brown"/></StackPanel>
 </StackPanel></Flyout></Button.Flyout></Button>
 <ComboBox x:Name="Size" Width="106" SelectedIndex="1" AutomationProperties.Name="Annotation size" ToolTipService.ToolTip="Annotation size"><ComboBoxItem Content="Small"/><ComboBoxItem Content="Medium"/><ComboBoxItem Content="Large"/></ComboBox>
 <ToggleButton x:Name="Eraser" MinWidth="40" ToolTipService.ToolTip="Erase a whole annotation" AutomationProperties.Name="Eraser"><Viewbox Width="22" Height="22"><Path Width="24" Height="24" Data="M3,14 L13,4 Q14,3 15,4 L21,10 Q22,11 21,12 L11,22 L9,22 L3,16 Q2,15 3,14 Z M9,8 L17,16 M9,22 L22,22" Stroke="#E6EBF5" StrokeThickness="1.7" StrokeLineJoin="Round" StrokeStartLineCap="Round" StrokeEndLineCap="Round"/></Viewbox></ToggleButton>
 <Button x:Name="Clear" Content="Clear" ToolTipService.ToolTip="Clear all annotations"/>
 <TextBlock x:Name="ToolHelp" Text="Draw on the picture. Select to move or resize." Foreground="#BBB9C8" FontSize="12" VerticalAlignment="Center" Margin="10,0,0,0"/>
</StackPanel></Grid>)");
        help = load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#151519"><StackPanel MaxWidth="460" HorizontalAlignment="Center" VerticalAlignment="Center" Spacing="18" Margin="24">
 <TextBlock x:Name="Help" Text="Choose your webcam" FontSize="20" TextAlignment="Center" TextWrapping="Wrap"/>
 <ComboBox x:Name="HelpCamera" PlaceholderText="Choose your webcam" HorizontalAlignment="Stretch" AutomationProperties.Name="Choose your webcam"/>
 <StackPanel Orientation="Horizontal" Spacing="12" HorizontalAlignment="Center"><Button x:Name="Retry" Content="Retry"/><Button x:Name="Privacy" Content="Camera privacy settings" Visibility="Collapsed"/></StackPanel>
</StackPanel></Grid>)");
        sidebarGate.Content(sidebar); toolbarGate.Content(toolbar);
        toolbarGate.Visibility(annotating?ux::Visibility::Visible:ux::Visibility::Collapsed);
        find<controls::Primitives::ToggleButton>(header,L"Annotate").IsChecked(annotating);
        for(auto gate:{sidebarGate,toolbarGate}) {gate.HorizontalContentAlignment(ux::HorizontalAlignment::Stretch);gate.VerticalContentAlignment(ux::VerticalAlignment::Stretch);}
        attach(headerIsland, header); attach(sidebarIsland, sidebarGate); attach(toolbarIsland, toolbarGate,false); attach(helpIsland, help);
        StartupTrace("islands-ready");
        for(auto island:{headerIsland,sidebarIsland,toolbarIsland,helpIsland}) {
            island.TakeFocusRequested([this](auto&& sender,auto&& args){
                bool reverse=args.Request().Reason()==hosting::XamlSourceFocusNavigationReason::Last;
                auto order=focusOrder();auto current=std::find(order.begin(),order.end(),sender);
                if(current==order.end()){SetFocus(window);return;}
                auto index=size_t(current-order.begin());
                index=reverse?(index+order.size()-1)%order.size():(index+1)%order.size();
                focusTarget(order[index],reverse);
            });
        }
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
        auto logo=(std::filesystem::path(executable).parent_path()/L"assets"/L"callayers.png").generic_wstring();
        find<controls::Image>(header,L"Logo").Source(ux::Media::Imaging::BitmapImage(winrt::Windows::Foundation::Uri(L"file:///"+logo)));
        BOOL dark = TRUE; DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        find<controls::MenuFlyoutItem>(header,L"Quit").Click([this](auto&&,auto&&){actions.quit();});
        find<controls::Primitives::ToggleButton>(header,L"Annotate").Click([this](auto&& sender,auto&&){annotating = sender.template as<controls::Primitives::ToggleButton>().IsChecked().GetBoolean();if(!annotating)actions.tool(AnnotationTool::Select);actions.layout();SaveAnnotationBarOpen(annotating);});
        auto cameraChange = [this](auto&& sender,auto&&) { if (!syncing) actions.camera(sender.template as<controls::ComboBox>().SelectedIndex()); };
        find<controls::ComboBox>(header,L"Camera").SelectionChanged(cameraChange);
        find<controls::ComboBox>(help,L"HelpCamera").SelectionChanged(cameraChange);
        find<controls::Button>(help,L"Retry").Click([this](auto&&,auto&&){actions.retry();});
        find<controls::Button>(header,L"RetryChoice").Click([this](auto&&,auto&&){actions.retry();});
        find<controls::Button>(help,L"Privacy").Click([this](auto&&,auto&&){actions.privacy();});
        find<controls::Button>(sidebar,L"HideAll").Click([this](auto&&,auto&&){actions.hideAll();});
        for(int i=0;i<3;++i) {
            auto number = std::to_wstring(i);
            auto checkbox=find<controls::CheckBox>(sidebar,(L"Show"+number).c_str()); checkbox.MinWidth(32); checkbox.Width(32);
            find<controls::Button>(sidebar,(L"Name"+number).c_str()).Click([this,i](auto&&,auto&&){actions.selectCard(i);});
            auto change = [this,i](auto&& sender,auto&&){if(!syncing) actions.visibility(i,sender.template as<controls::CheckBox>().IsChecked().GetBoolean());};
            auto check = find<controls::CheckBox>(sidebar,(L"Show"+number).c_str()); check.Checked(change); check.Unchecked(change);
        }
        find<controls::TextBox>(sidebar,L"Search").TextChanged([this](auto&& sender,auto&&){
            std::wstring query(sender.template as<controls::TextBox>().Text());
            std::transform(query.begin(),query.end(),query.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
            bool any=false;
            for(int i=0;i<3;++i) {
                std::wstring name(names[i]); std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
                bool show=name.find(query)!=std::wstring::npos; any |= show;
                find<controls::Border>(sidebar,(L"Row"+std::to_wstring(i)).c_str()).Visibility(show?ux::Visibility::Visible:ux::Visibility::Collapsed);
            }
            find<controls::TextBlock>(sidebar,L"NoResults").Visibility(any?ux::Visibility::Collapsed:ux::Visibility::Visible);
        });
        for(auto entry : {std::pair{L"Select",AnnotationTool::Select}, {L"Pen",AnnotationTool::Pen}, {L"Text",AnnotationTool::Text}, {L"Eraser",AnnotationTool::Eraser}})
            find<controls::Primitives::ToggleButton>(toolbar,entry.first).Click([this,type=entry.second](auto&&,auto&&){actions.tool(type);});
        for(auto entry : {std::pair{L"Rectangle",AnnotationTool::Rectangle}, {L"Ellipse",AnnotationTool::Ellipse}, {L"Line",AnnotationTool::Line}, {L"Arrow",AnnotationTool::Arrow}})
            find<controls::MenuFlyoutItem>(toolbar,entry.first).Click([this,type=entry.second](auto&&,auto&&){actions.tool(type);});
        const wchar_t* colors[]={L"#000000",L"#808080",L"#ffffff",L"#ff0000",L"#ff8000",L"#ffd166",L"#00aa00",L"#00bfff",L"#0000ff",L"#8000ff",L"#ff00ff",L"#804000"};
        for(int i=0;i<12;++i)find<controls::Button>(toolbar,(L"Color"+std::to_wstring(i)).c_str()).Click([this,color=std::wstring(colors[i])](auto&&,auto&&){actions.color(color);find<controls::Button>(toolbar,L"Color").Flyout().Hide();});
        find<controls::ComboBox>(toolbar,L"Size").SelectionChanged([this](auto&& sender,auto&&){if(!syncing)actions.size(sender.template as<controls::ComboBox>().SelectedIndex());});
        find<controls::Button>(toolbar,L"Clear").Click([this](auto&&,auto&&){actions.clear();});
        Resources().event("workspace_controls_ready");
        StartupTrace("controls-ready");
    }
    ~Impl() { lifetime.reset();editor.close();helpIsland.Close(); toolbarIsland.Close(); sidebarIsland.Close(); headerIsland.Close(); }
};
Workspace::Workspace(HWND window,WorkspaceActions actions) : impl_(std::make_unique<Impl>(window,std::move(actions))) {}
Workspace::~Workspace() = default;
int Workspace::toolbarHeight() const { return impl_->annotating?56:0; }
void Workspace::cameras(const std::vector<std::wstring>& names,int selected,const std::wstring& missingName) {
    auto& ui=*impl_; ui.syncing=true;
    for(auto combo : {ui.find<controls::ComboBox>(ui.header,L"Camera"), ui.find<controls::ComboBox>(ui.help,L"HelpCamera")}) {
        combo.Items().Clear(); for(const auto& name:names) combo.Items().Append(winrt::box_value(name)); combo.SelectedIndex(selected);
        combo.PlaceholderText(missingName.empty()?L"Choose your webcam":missingName+L" (not connected)");
    }
    ui.syncing=false;
}
void Workspace::card(int selected,const bool* visible) {
    auto& ui=*impl_;
    if(ui.selected==selected && std::equal(visible,visible+3,ui.listedVisible))return;
    ui.selected=selected;std::copy(visible,visible+3,ui.listedVisible);ui.syncing=true;
    for(int i=0;i<3;++i) {
        auto number=std::to_wstring(i);
        ui.find<controls::CheckBox>(ui.sidebar,(L"Show"+number).c_str()).IsChecked(visible[i]);
        ui.find<controls::Border>(ui.sidebar,(L"Row"+number).c_str()).Background(ux::Media::SolidColorBrush(selected==i?winrt::Windows::UI::Color{255,59,57,83}:winrt::Windows::UI::Color{0,0,0,0}));
    }
    ui.find<controls::TextBlock>(ui.sidebar,L"Selection").Text(selected>=0?Impl::names[selected]:L"Select a card");
    ui.find<controls::TextBlock>(ui.sidebar,L"SelectionHelp").Text(selected>=0?(visible[selected]?L"Move this card in the preview or drag its corner to resize.":L"This card is hidden. Check its box to show it on camera."):L"Check a box to show a card. Select its name to work with it.");
    ui.syncing=false;
}
void Workspace::annotationControls(AnnotationTool tool,const std::wstring& color,int size) {
    auto& ui=*impl_;ui.syncing=true;
    for(auto entry : {std::pair{L"Select",AnnotationTool::Select}, {L"Pen",AnnotationTool::Pen}, {L"Text",AnnotationTool::Text}, {L"Eraser",AnnotationTool::Eraser}})
        ui.find<controls::Primitives::ToggleButton>(ui.toolbar,entry.first).IsChecked(tool==entry.second);
    bool shape=tool>=AnnotationTool::Rectangle && tool<=AnnotationTool::Arrow;
    ui.find<controls::DropDownButton>(ui.toolbar,L"Shapes").Background(ux::Media::SolidColorBrush(shape?winrt::Windows::UI::Color{255,77,70,120}:winrt::Windows::UI::Color{255,48,48,54}));
    unsigned value=unsigned(std::wcstoul(color.c_str()+1,nullptr,16));
    ui.find<controls::Border>(ui.toolbar,L"ColorSwatch").Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{255,uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)}));
    ui.find<controls::ComboBox>(ui.toolbar,L"Size").SelectedIndex(size);ui.syncing=false;
}
bool Workspace::editingText() const {return impl_->editor.active();}
bool Workspace::finishText(bool commit) {
    auto& ui=*impl_;if(!ui.editor.active())return true;if(ui.finishingText)return false;
    ui.finishingText=true;
    bool accepted=ui.actions.text(ui.editor.text(),commit,ui.editor.height());
    if(accepted){++ui.editorToken;ui.editor.close();}
    ui.finishingText=false;
    if(!accepted && ui.editor.active())ui.editor.focus();
    return accepted;
}
void Workspace::editText(const RECT& area,const std::wstring& value,double font,const std::wstring& color,double width,double scale,double minimumHeight) {
    auto& ui=*impl_;if(!finishText())return;
    auto token=++ui.editorToken;auto weak=std::weak_ptr<int>(ui.lifetime);
    ui.editor.open(ui.window,area,value,font,color,width,scale,minimumHeight,[this,weak,token](bool commit){
        if(weak.expired())return;
        impl_->header.DispatcherQueue().TryEnqueue([this,weak,token,commit]{
            if(weak.expired() || impl_->editorToken!=token)return;
            bool unavailable=impl_->editor.unavailable();
            if(impl_->editor.active())finishText(commit);
            if(unavailable){Resources().event("text_editor_open_failed");MessageBoxW(impl_->window,L"Couldn't open text editing. Try again.",L"Text editing",MB_OK|MB_ICONINFORMATION);}
        });
    });
}
void Workspace::focus(bool reverse) {
    auto order=impl_->focusOrder();impl_->focusTarget(reverse?order.back():order.front(),reverse);
}
void Workspace::state(const std::wstring& status,const std::wstring& message,bool fresh,bool privacy,bool retryPreference) {
    auto& ui=*impl_;
    if(ui.lastStatus!=status){ui.find<controls::TextBlock>(ui.header,L"Status").Text(status);ui.lastStatus=status;}
    if(ui.lastHelp!=message){ui.find<controls::TextBlock>(ui.help,L"Help").Text(message);ui.lastHelp=message;}
    if(ui.lastPrivacy!=privacy){ui.find<controls::Button>(ui.help,L"Privacy").Visibility(privacy?ux::Visibility::Visible:ux::Visibility::Collapsed);ui.lastPrivacy=privacy;}
    if(ui.lastRetryPreference!=retryPreference){ui.find<controls::Button>(ui.header,L"RetryChoice").Visibility(retryPreference?ux::Visibility::Visible:ux::Visibility::Collapsed);ui.lastRetryPreference=retryPreference;}
    if(ui.lastFresh!=fresh){
        ui.sidebarGate.IsEnabled(fresh); ui.toolbarGate.IsEnabled(fresh);
        if(fresh)ui.helpIsland.SiteBridge().Hide();else ui.helpIsland.SiteBridge().Show();ui.lastFresh=fresh;
    }
}
void Workspace::layout(int width,int height,float dpi,const RECT& preview) {
    auto& ui=*impl_; int header=int(72*dpi), sidebar=int(300*dpi), toolbar=int(toolbarHeight()*dpi);
    ui.headerIsland.SiteBridge().MoveAndResize({0,0,width,header});
    ui.sidebarIsland.SiteBridge().MoveAndResize({0,header,sidebar,std::max(1,height-header)});
    ui.toolbarIsland.SiteBridge().MoveAndResize({sidebar,header,std::max(1,width-sidebar),std::max(1,toolbar)});
    ui.toolbarGate.Visibility(ui.annotating?ux::Visibility::Visible:ux::Visibility::Collapsed);
    ui.find<controls::TextBlock>(ui.toolbar,L"ToolHelp").Visibility(width/dpi>=1450?ux::Visibility::Visible:ux::Visibility::Collapsed);
    if(toolbar)ui.toolbarIsland.SiteBridge().Show();else ui.toolbarIsland.SiteBridge().Hide();
    ui.helpIsland.SiteBridge().MoveAndResize({preview.left,preview.top,std::max(1L,preview.right-preview.left),std::max(1L,preview.bottom-preview.top)});
}
}
