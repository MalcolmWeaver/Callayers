#include "workspace.h"
#include "resource_diagnostics.h"
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
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>

namespace callayers {
namespace ux = winrt::Microsoft::UI::Xaml;
namespace controls = ux::Controls;
namespace hosting = ux::Hosting;
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
    bool syncing = false, annotating = true;
    bool lastFresh = true, lastPrivacy = false;
    bool lastRetryPreference = false;
    bool listedVisible[3]{true,true,true};
    std::wstring lastStatus, lastHelp;
    int selected = -1;
    static constexpr const wchar_t* names[] = {L"Name card", L"Animated card", L"Five-minute timer"};
    template<class T> T find(ux::FrameworkElement const& root, const wchar_t* name) { return root.FindName(name).as<T>(); }
    static ux::FrameworkElement load(const wchar_t* xaml) { return ux::Markup::XamlReader::Load(xaml).as<ux::FrameworkElement>(); }
    void attach(hosting::DesktopWindowXamlSource const& island, ux::FrameworkElement const& content) {
        island.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        content.RequestedTheme(ux::ElementTheme::Dark); island.Content(content);
        island.SiteBridge().Show();
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
 <ToggleButton Grid.Column="2" x:Name="Annotate" Content="✎  Annotate" IsChecked="True" VerticalAlignment="Center"/>
 <ComboBox Grid.Column="3" x:Name="Camera" PlaceholderText="Choose your webcam" Width="220" HorizontalAlignment="Right" VerticalAlignment="Center" AutomationProperties.Name="Webcam"/>
 <StackPanel Grid.Column="4" Width="180" VerticalAlignment="Center" Spacing="2"><TextBlock x:Name="Status" FontSize="12" Foreground="#BBB9C8" TextWrapping="Wrap"/><Button x:Name="RetryChoice" Content="Retry" Visibility="Collapsed" ToolTipService.ToolTip="Remember this webcam again"/></StackPanel>
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
 <TextBlock Text="These sample cards and edits are temporary." FontSize="12" Foreground="#BBB9C8" TextWrapping="Wrap"/>
 </StackPanel></ScrollViewer>
 <StackPanel Grid.Row="2" Spacing="8"><Button x:Name="UseInCall" Content="Use in a call" HorizontalAlignment="Stretch"><Button.Flyout><Flyout><TextBlock Text="In your calling app, choose Callayers as your camera." TextWrapping="Wrap" Width="240"/></Flyout></Button.Flyout></Button><TextBlock Text="In your calling app, choose Callayers" FontSize="12" Foreground="#BBB9C8" TextWrapping="Wrap"/></StackPanel>
</Grid>)");
        toolbar = load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#1D1D21" Padding="20,0"><StackPanel Orientation="Horizontal" Spacing="8" VerticalAlignment="Center">
 <ToggleButton x:Name="Select" Content="↖  Select" IsChecked="True" ToolTipService.ToolTip="Select, move or resize a card"/>
 <ToggleButton x:Name="Pen" Content="✎  Pen" ToolTipService.ToolTip="Draw on the presentation"/>
 <Button x:Name="Clear" Content="Clear annotations"/>
 <TextBlock x:Name="ToolHelp" Text="Drag a card to move it; drag its corner to resize." Foreground="#BBB9C8" FontSize="12" VerticalAlignment="Center" Margin="12,0,0,0"/>
</StackPanel></Grid>)");
        help = load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#151519"><StackPanel MaxWidth="460" HorizontalAlignment="Center" VerticalAlignment="Center" Spacing="18" Margin="24">
 <TextBlock x:Name="Help" Text="Choose your webcam" FontSize="20" TextAlignment="Center" TextWrapping="Wrap"/>
 <ComboBox x:Name="HelpCamera" PlaceholderText="Choose your webcam" HorizontalAlignment="Stretch" AutomationProperties.Name="Choose your webcam"/>
 <StackPanel Orientation="Horizontal" Spacing="12" HorizontalAlignment="Center"><Button x:Name="Retry" Content="Retry"/><Button x:Name="Privacy" Content="Camera privacy settings" Visibility="Collapsed"/></StackPanel>
</StackPanel></Grid>)");
        sidebarGate.Content(sidebar); toolbarGate.Content(toolbar);
        for(auto gate:{sidebarGate,toolbarGate}) {gate.HorizontalContentAlignment(ux::HorizontalAlignment::Stretch);gate.VerticalContentAlignment(ux::VerticalAlignment::Stretch);}
        attach(headerIsland, header); attach(sidebarIsland, sidebarGate); attach(toolbarIsland, toolbarGate); attach(helpIsland, help);
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
        find<controls::Primitives::ToggleButton>(header,L"Annotate").Click([this](auto&&,auto&&){annotating = !annotating; actions.layout();});
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
        auto tool=[this](bool pen){
            find<controls::Primitives::ToggleButton>(toolbar,L"Select").IsChecked(!pen);
            find<controls::Primitives::ToggleButton>(toolbar,L"Pen").IsChecked(pen); actions.pen(pen);
        };
        find<controls::Primitives::ToggleButton>(toolbar,L"Select").Click([tool](auto&&,auto&&){tool(false);});
        find<controls::Primitives::ToggleButton>(toolbar,L"Pen").Click([tool](auto&&,auto&&){tool(true);});
        find<controls::Button>(toolbar,L"Clear").Click([this](auto&&,auto&&){actions.clear();});
        Resources().event("workspace_controls_ready");
        StartupTrace("controls-ready");
    }
    ~Impl() { helpIsland.Close(); toolbarIsland.Close(); sidebarIsland.Close(); headerIsland.Close(); }
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
void Workspace::penMode(bool pen) {
    auto& ui=*impl_;
    ui.find<controls::Primitives::ToggleButton>(ui.toolbar,L"Select").IsChecked(!pen);
    ui.find<controls::Primitives::ToggleButton>(ui.toolbar,L"Pen").IsChecked(pen);
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
    ui.find<controls::TextBlock>(ui.toolbar,L"ToolHelp").Visibility(width/dpi>=1200?ux::Visibility::Visible:ux::Visibility::Collapsed);
    if(toolbar)ui.toolbarIsland.SiteBridge().Show();else ui.toolbarIsland.SiteBridge().Hide();
    ui.helpIsland.SiteBridge().MoveAndResize({preview.left,preview.top,std::max(1L,preview.right-preview.left),std::max(1L,preview.bottom-preview.top)});
}
}
