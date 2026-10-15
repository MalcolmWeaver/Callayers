#include "workspace.h"
#include "resource_diagnostics.h"
#include "text_editor.h"
#include "release_updates.h"
#include <winrt/Windows.Data.Json.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <MddBootstrap.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <Microsoft.UI.Dispatching.Interop.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
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
#include <cmath>

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
    hosting::DesktopWindowXamlSource headerIsland, sidebarIsland, toolbarIsland, helpIsland,authIsland;
    ux::FrameworkElement header{nullptr}, sidebar{nullptr}, toolbar{nullptr}, help{nullptr},authView{nullptr};
    controls::ContentControl sidebarGate, toolbarGate;
    TextEditor editor;
    bool finishingText=false;
    unsigned editorToken=0;
    std::shared_ptr<int> lifetime=std::make_shared<int>(0);
    bool syncing = false, annotating = LoadAnnotationBarOpen();
    bool authorized=false;
    bool lastFresh = true, lastPrivacy = false;
    bool lastCameraChoices = false;
    bool lastRetryPreference = false;
    bool listedVisible[3]{true,true,true};
    std::wstring lastStatus, lastHelp;
    controls::Flyout about{nullptr};
    controls::TextBlock updateMessage{nullptr};
    controls::Button updateButton{nullptr};
    HANDLE updateProcess=nullptr;
    ULONGLONG updatePoll=0;
    bool updateReady=false;
    bool aboutOpen=false;
    std::wstring updateState;
    void showUpdate(const std::wstring& state,const std::wstring& version=L"") {
        updateState=state;updateReady=state==L"ready" || state==L"waiting-for-camera";
        bool busy=state==L"checking" || state==L"downloading";
        updateButton.Content(winrt::box_value(updateReady?L"Restart to update":busy?(state==L"checking"?L"Checking…":L"Downloading…"):L"Check for updates"));
        updateButton.IsEnabled(!busy && !ReleaseRoot().empty());
        updateMessage.Text(state==L"development"?L"Updates are available in installed releases.":state==L"current"?L"You’re up to date.":state==L"checking"?L"Checking for updates…":state==L"downloading"?L"Downloading version "+version+L"…":state==L"ready"?L"Version "+version+L" is ready to install.":state==L"waiting-for-camera"?L"Close apps using the Callayers camera, then restart to update.":state==L"deferred"?L"Couldn’t complete the update. Check your connection and try again.":L"Updates install when Callayers and its camera are closed.");
    }
    void checkUpdate() {
        if(updateProcess)return;
        showUpdate(L"checking");
        if(!StartReleaseUpdate(L"Stage",0,&updateProcess))showUpdate(ReleaseRoot().empty()?L"development":L"deferred");
    }
    void pollUpdate() {
        if(!aboutOpen && !updateProcess)return;
        if(GetTickCount64()-updatePoll<500)return;
        updatePoll=GetTickCount64();
        bool running=updateProcess && WaitForSingleObject(updateProcess,0)==WAIT_TIMEOUT;
        if(updateProcess && !running){CloseHandle(updateProcess);updateProcess=nullptr;}
        auto root=ReleaseRoot();if(root.empty())return;
        try {
            std::ifstream file(root/L"update-status.json");
            std::string bytes((std::istreambuf_iterator<char>(file)),{});
            auto json=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(bytes));
            auto state=std::wstring(json.GetNamedString(L"state"));
            auto version=std::wstring(json.GetNamedString(L"version",L""));
            // A prior completed check must not enable restart during a new check.
            if(running){if(state==L"downloading")showUpdate(state,version);return;}
            if(state==L"checking" || state==L"downloading")state=L"deferred";
            if(state==L"installed")state=L"current";
            if(state==L"ready" && updateState==L"waiting-for-camera")return;
            if(state!=updateState)showUpdate(state,version);
        }catch(...){if(!running && (updateState==L"checking" || updateState==L"downloading"))showUpdate(L"deferred");}
    }
    int selected = -1;
#ifdef CALLAYERS_GENERATION
    controls::TextBox prompt{nullptr};controls::Button generateButton{nullptr},cancelButton{nullptr};controls::TextBlock generationMessage{nullptr};
    controls::StackPanel generatedPanel{nullptr};std::vector<std::wstring> generatedNames;
    std::vector<controls::CheckBox> generatedChecks;std::vector<controls::Border> generatedRows;
    std::wstring lastGenerationMessage;bool lastGenerating=false;
#ifdef CALLAYERS_LIBRARY
    struct LibraryItem {std::wstring id,name;bool previous=false;unsigned long long edited=0;};
    std::vector<LibraryItem> library;
    // Keep projected editor controls alive for the lifetime of their rows.
    // Event handlers use weak references to avoid cycles through the visual tree.
    std::vector<controls::TextBox> libraryRenameFields;
    std::vector<controls::TextBlock> libraryNameLabels;
    std::wstring selectedId,editingId,submittedTarget,submittedPrompt,requestName;
    controls::TextBlock composerTitle{nullptr},composerDescription{nullptr};
    controls::ProgressRing composerProgress{nullptr};
    controls::ScrollViewer composerStatus{nullptr};
    controls::StackPanel attachmentPanel{nullptr};
    std::vector<TextAttachment> attachments;
    bool editingPaste=false;
    size_t draftBytes()const{size_t bytes=winrt::to_string(prompt.Text()).size();for(auto& source:attachments)bytes+=winrt::to_string(source.text).size();return bytes;}
    void refreshAttachments(){
        attachmentPanel.Children().Clear();
        for(size_t i=0;i<attachments.size();++i){
            auto tile=load(LR"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#303036" BorderBrush="#44444C" BorderThickness="1" CornerRadius="10" Padding="8"><Grid ColumnSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="32"/><ColumnDefinition Width="*"/><ColumnDefinition Width="24"/></Grid.ColumnDefinitions><Border Background="#3A3A42" CornerRadius="7" Width="32" Height="40" VerticalAlignment="Top"><FontIcon Glyph="&#xE8A5;" FontSize="18"/></Border><StackPanel Grid.Column="1" Spacing="2"><TextBlock x:Name="Title" FontSize="12" FontWeight="SemiBold" TextTrimming="CharacterEllipsis"/><TextBlock x:Name="Count" FontSize="10" Foreground="#BBB9C8"/><HyperlinkButton x:Name="Show" Content="Show in text field" FontSize="11" Padding="0" MinHeight="20" HorizontalAlignment="Left"/></StackPanel><Button x:Name="Remove" Grid.Column="2" Content="×" Padding="0" MinWidth="24" Width="24" Height="24" MinHeight="24" CornerRadius="12" VerticalAlignment="Top" AutomationProperties.Name="Remove pasted text" ToolTipService.ToolTip="Remove pasted text"/></Grid></Border>)");
            find<controls::TextBlock>(tile,L"Title").Text(attachments[i].name);controls::ToolTipService::SetToolTip(tile,winrt::box_value(attachments[i].name));
            find<controls::TextBlock>(tile,L"Count").Text(std::to_wstring(attachments[i].text.size())+L" characters");
            find<controls::Button>(tile,L"Remove").Click([this,i](auto&&,auto&&){if(lastGenerating)return;attachments.erase(attachments.begin()+i);refreshAttachments();refreshComposer();});
            find<controls::HyperlinkButton>(tile,L"Show").Click([this,i](auto&&,auto&&){if(lastGenerating)return;auto value=std::wstring(prompt.Text());if(!value.empty())value+=L"\n\n";value+=attachments[i].text;attachments.erase(attachments.begin()+i);editingPaste=true;prompt.Text(value);refreshAttachments();refreshComposer();prompt.Focus(ux::FocusState::Programmatic);});
            attachmentPanel.Children().Append(tile);
        }
        attachmentPanel.Visibility(attachments.empty()?ux::Visibility::Collapsed:ux::Visibility::Visible);
        find<controls::ScrollViewer>(sidebar,L"AttachmentScroll").Visibility(attachments.empty()?ux::Visibility::Collapsed:ux::Visibility::Visible);
    }
    void clearDraft(){attachments.clear();editingPaste=false;prompt.Text(L"");refreshAttachments();}
    void pasteText(controls::TextControlPasteEventArgs const& args){
        // Handle every text paste before WinUI can apply an implicit control limit.
        args.Handled(true);if(lastGenerating)return;
        if(!OpenClipboard(window)){showComposerMessage(L"Couldn't read the clipboard. Try pasting again.");return;}
        struct ClipboardGuard{~ClipboardGuard(){CloseClipboard();}} guard;
        auto data=GetClipboardData(CF_UNICODETEXT);if(!data){showComposerMessage(L"Paste plain text here.");return;}
        auto bytes=GlobalSize(data);if(bytes>2000000){showComposerMessage(L"This paste is too large. Use up to 100 KB of text. Your draft was kept.");return;}
        auto raw=static_cast<const wchar_t*>(GlobalLock(data));if(!raw){showComposerMessage(L"Couldn't read the clipboard. Try pasting again.");return;}
        size_t count=0;while(count<bytes/sizeof(wchar_t) && raw[count])++count;std::wstring text(raw,count);GlobalUnlock(data);
        bool attach=text.size()>=1000 && text.find_first_not_of(L" \t\r\n")!=std::wstring::npos;auto value=std::wstring(prompt.Text());auto start=size_t(prompt.SelectionStart()),length=size_t(prompt.SelectionLength());
        auto total=draftBytes()+winrt::to_string(text).size();if(!attach)total-=winrt::to_string(value.substr(start,length)).size();
        if(total>GenerationTextLimit || (attach && attachments.size()>=8)){showComposerMessage(L"This paste would exceed 100 KB of text or 8 attachments. Your draft was kept; the paste is still on your clipboard.");return;}
        if(attach){auto first=text.find_first_not_of(L" \t\r\n");auto title=text.substr(first,72);if(!title.empty() && title.back()>=0xD800 && title.back()<=0xDBFF)title.pop_back();std::replace_if(title.begin(),title.end(),[](wchar_t c){return c<L' ';},L' ');attachments.push_back({title,std::move(text)});refreshAttachments();}
        else{value.replace(start,length,text);prompt.Text(value);prompt.SelectionStart(int(start+text.size()));prompt.SelectionLength(0);}
        showComposerMessage(L"");refreshComposer();
    }
    void showComposerMessage(const std::wstring& value){generationMessage.Text(value);composerStatus.Visibility(value.empty()?ux::Visibility::Collapsed:ux::Visibility::Visible);}
    std::wstring libraryName(const std::wstring& id)const{for(auto& card:library)if(card.id==id)return card.name;return L"";}
    void refreshComposer(){
        auto name=libraryName(editingId);composerTitle.Text(editingId.empty()?L"New card":L"Edit “"+name+L"”");controls::ToolTipService::SetToolTip(composerTitle,winrt::box_value(std::wstring(composerTitle.Text())));
        composerDescription.Text(lastGenerating?(requestName.empty()?L"Creating a new card…":L"Updating this card…"):editingId.empty()?L"New cards appear in your preview automatically.":L"Your current card stays visible.");
        prompt.PlaceholderText(editingId.empty()?L"Describe your card…":L"Describe the changes…");prompt.IsEnabled(!lastGenerating);
        generateButton.IsEnabled(!lastGenerating && (!attachments.empty() || std::wstring(prompt.Text()).find_first_not_of(L" \t\r\n")!=std::wstring::npos));
        for(auto const& child:attachmentPanel.Children()){auto tile=child.as<ux::FrameworkElement>();find<controls::Button>(tile,L"Remove").IsEnabled(!lastGenerating);find<controls::HyperlinkButton>(tile,L"Show").IsEnabled(!lastGenerating);}
        auto value=std::wstring(prompt.Text());size_t lines=1;for(wchar_t c:value)if(c==L'\n')++lines;
        prompt.Height(editingPaste?180.0:std::clamp(36.0+20.0*std::max(double(lines),std::ceil(double(value.size())/32.0)),56.0,140.0));
        cancelButton.Visibility(lastGenerating?ux::Visibility::Visible:ux::Visibility::Collapsed);
        composerProgress.IsActive(lastGenerating);composerProgress.Visibility(lastGenerating?ux::Visibility::Visible:ux::Visibility::Collapsed);
    }
    void openCreate(){if(lastGenerating)return;actions.selectCard(-1);editingId.clear();clearDraft();showComposerMessage(L"");refreshComposer();prompt.Focus(ux::FocusState::Programmatic);}
    void openEdit(const std::wstring& id){if(lastGenerating)return;for(size_t i=0;i<library.size();++i)if(library[i].id==id){actions.selectCard(int(i));if(editingId!=id)clearDraft();editingId=id;showComposerMessage(L"");refreshComposer();prompt.Focus(ux::FocusState::Programmatic);return;}}
    void sendMessage(){if(lastGenerating)return;auto value=std::wstring(prompt.Text());if(attachments.empty() && value.find_first_not_of(L" \t\r\n")==std::wstring::npos)return;
        if(draftBytes()>GenerationTextLimit){showComposerMessage(L"This draft is too large. Use up to 100 KB of text. Your draft was kept.");return;}
        submittedTarget=editingId;submittedPrompt=value;requestName=libraryName(editingId);lastGenerating=true;showComposerMessage(L"");refreshComposer();
        if(editingId.empty())actions.generate(value,attachments);else actions.editCard(editingId,value,attachments);
    }
    void filterLibrary(){std::wstring query(find<controls::TextBox>(sidebar,L"Search").Text());std::transform(query.begin(),query.end(),query.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});bool any=false;
        for(size_t i=0;i<library.size();++i){auto name=library[i].name;std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});bool show=name.find(query)!=std::wstring::npos;any|=show;generatedRows[i].Visibility(show?ux::Visibility::Visible:ux::Visibility::Collapsed);}
        auto message=find<controls::TextBlock>(sidebar,L"NoResults");message.Text(library.empty()?L"Create your first card":L"No matching cards");message.Visibility(any?ux::Visibility::Collapsed:ux::Visibility::Visible);
    }
#endif
#endif
    static constexpr const wchar_t* names[] = {
#ifdef CALLAYERS_INTERACTIVE
        L"Interactive card",
#else
        L"Name card",
#endif
        L"Animated card", L"Five-minute timer"};
    template<class T> T find(ux::FrameworkElement const& root, const wchar_t* name) { return root.FindName(name).as<T>(); }
    static ux::FrameworkElement load(const wchar_t* xaml) { return ux::Markup::XamlReader::Load(xaml).as<ux::FrameworkElement>(); }
    void attach(hosting::DesktopWindowXamlSource const& island, ux::FrameworkElement const& content,bool visible=true) {
        island.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(window));
        content.RequestedTheme(ux::ElementTheme::Dark); island.Content(content);
        if(visible)island.SiteBridge().Show();else island.SiteBridge().Hide();
    }
    std::vector<hosting::DesktopWindowXamlSource> focusOrder() {
        if(!authorized)return {authIsland};
        if(!lastFresh) {
            if(lastCameraChoices)return {headerIsland,helpIsland};
            return {headerIsland};
        }
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
 <Button x:Name="File" Grid.Column="1" Content="File" VerticalAlignment="Center"><Button.Flyout><MenuFlyout><MenuFlyoutItem x:Name="Account" Text="Signed in" IsEnabled="False"/><MenuFlyoutItem x:Name="SignOut" Text="Sign out"/><MenuFlyoutSeparator/><MenuFlyoutItem x:Name="About" Text="About Callayers"/><MenuFlyoutItem x:Name="Quit" Text="Quit"/></MenuFlyout></Button.Flyout></Button>
 <ToggleButton Grid.Column="2" x:Name="Annotate" Content="✎  Annotate" IsChecked="False" VerticalAlignment="Center"/>
 <ComboBox Grid.Column="3" x:Name="Camera" PlaceholderText="Choose your webcam" Width="220" HorizontalAlignment="Right" VerticalAlignment="Center" AutomationProperties.Name="Webcam"/>
 <StackPanel Grid.Column="4" Width="180" VerticalAlignment="Center" Spacing="2"><TextBlock x:Name="Status" FontSize="12" Foreground="#BBB9C8" TextWrapping="Wrap"/><Button x:Name="RetryChoice" Content="Retry" Visibility="Collapsed" ToolTipService.ToolTip="Try again"/></StackPanel>
</Grid>)");
        sidebar = load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#242428" Padding="20" RowSpacing="16">
 <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
 <StackPanel Spacing="12"><TextBlock Text="Cards" FontSize="22" FontWeight="SemiBold"/>
 <Button x:Name="CreateAI" Content="+  Create with AI" HorizontalAlignment="Stretch" IsEnabled="False" ToolTipService.ToolTip="Create a card from a prompt."/>
 <StackPanel Orientation="Horizontal" Spacing="8"><Button x:Name="ImportImage" Content="Import image" IsEnabled="False" ToolTipService.ToolTip="Import a local image."/><Button x:Name="HideAll" Content="Hide all"/></StackPanel>
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
#ifdef CALLAYERS_LIBRARY
        sidebar=load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#242428" Padding="16" RowSpacing="12">
 <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
 <StackPanel Spacing="10"><TextBlock Text="Cards" FontSize="22" FontWeight="SemiBold"/>
 <Button x:Name="CreateAI" Content="+  Create with AI" HorizontalAlignment="Stretch" ToolTipService.ToolTip="Create a card from a prompt."/>
 <Grid ColumnSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Button x:Name="ImportImage" Content="Import image" HorizontalAlignment="Stretch"/><Button x:Name="HideAll" Grid.Column="1" Content="Hide all" HorizontalAlignment="Stretch"/></Grid>
 <TextBox x:Name="Search" PlaceholderText="Search cards" AutomationProperties.Name="Search cards"/></StackPanel>
 <ScrollViewer Grid.Row="1" HorizontalScrollBarVisibility="Disabled" VerticalScrollBarVisibility="Auto" Padding="0,0,4,0"><StackPanel Spacing="8"><StackPanel x:Name="Rows" Spacing="4"/><TextBlock x:Name="NoResults" Text="Create your first card" Foreground="#BBB9C8" TextWrapping="Wrap" Margin="4,12"/></StackPanel></ScrollViewer>
 <StackPanel Grid.Row="2" Spacing="6"><Button x:Name="UseInCall" Content="Use in a call" HorizontalAlignment="Stretch"><Button.Flyout><Flyout><TextBlock Text="In your calling app, choose Callayers as your camera." TextWrapping="Wrap" Width="240"/></Flyout></Button.Flyout></Button></StackPanel>
 <Border Grid.Row="3" Background="#29292F" BorderBrush="#44444C" BorderThickness="1" CornerRadius="16" Padding="10"><StackPanel Spacing="6">
 <Grid ColumnSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><TextBlock x:Name="ComposerTarget" Text="New card" FontSize="13" FontWeight="SemiBold" TextTrimming="CharacterEllipsis" VerticalAlignment="Center"/><ProgressRing x:Name="ComposerProgress" Grid.Column="1" Width="16" Height="16" IsActive="False" Visibility="Collapsed"/></Grid>
 <TextBlock x:Name="ComposerDescription" FontSize="11" Foreground="#BBB9C8" TextWrapping="Wrap" MaxHeight="48"/>
 <ScrollViewer MaxHeight="180" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled" Visibility="Collapsed" x:Name="AttachmentScroll"><StackPanel x:Name="ComposerAttachments" Spacing="6"/></ScrollViewer>
 <TextBox x:Name="ComposerPrompt" PlaceholderText="Describe your card…" AutomationProperties.Name="Card message" AcceptsReturn="True" TextWrapping="Wrap" Height="56" MinHeight="56" FontSize="13" Padding="8,6" ScrollViewer.VerticalScrollBarVisibility="Auto" ToolTipService.ToolTip="Enter adds a line. Ctrl+Enter sends your message."/>
 <TextBlock Text="Ctrl+Enter to send · Links aren't opened" FontSize="10" Foreground="#BBB9C8" TextWrapping="Wrap"/>
 <StackPanel Orientation="Horizontal" Spacing="6"><Button x:Name="ComposerSend" Content="Send" MinHeight="28" FontSize="12" Padding="12,2" Background="#A39BFA" Foreground="#201D33"/><Button x:Name="ComposerCancel" Content="Cancel" MinHeight="28" FontSize="12" Padding="12,2" Visibility="Collapsed"/></StackPanel>
 <ScrollViewer x:Name="ComposerStatus" MaxHeight="48" Visibility="Collapsed" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled"><TextBlock x:Name="ComposerMessage" TextWrapping="Wrap" FontSize="11" Foreground="#BBB9C8" IsTextSelectionEnabled="True"/></ScrollViewer>
 </StackPanel></Border>
</Grid>)");
#endif
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
 <TextBlock x:Name="Help" FontSize="20" TextAlignment="Center" TextWrapping="Wrap"/>
 <ComboBox x:Name="HelpCamera" PlaceholderText="Choose your webcam" HorizontalAlignment="Stretch" AutomationProperties.Name="Choose your webcam" Visibility="Collapsed"/>
 <StackPanel x:Name="HelpActions" Orientation="Horizontal" Spacing="12" HorizontalAlignment="Center" Visibility="Collapsed"><Button x:Name="Retry" Content="Retry"/><Button x:Name="Privacy" Content="Camera privacy settings" Visibility="Collapsed"/></StackPanel>
</StackPanel></Grid>)");
        authView=load(LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#1D1D21" Padding="24"><StackPanel Width="420" HorizontalAlignment="Center" VerticalAlignment="Center" Spacing="24">
 <Image x:Name="AuthLogo" Width="76" Height="76" AutomationProperties.Name="Callayers logo"/>
 <TextBlock Text="Callayers" FontSize="32" FontWeight="SemiBold" HorizontalAlignment="Center"/>
 <TextBlock x:Name="AuthMessage" Text="Sign in to use Callayers." FontSize="16" Foreground="#BBB9C8" TextAlignment="Center" TextWrapping="Wrap"/>
 <ProgressRing x:Name="AuthProgress" Width="28" Height="28" Visibility="Collapsed" IsActive="False"/>
 <Button x:Name="AuthPrimary" Content="Sign in with Google" HorizontalAlignment="Center" MinWidth="240" Padding="24,12"/>
 <Button x:Name="AuthCancel" Content="Cancel" HorizontalAlignment="Center" Visibility="Collapsed"/>
</StackPanel></Grid>)");
        sidebarGate.Content(sidebar); toolbarGate.Content(toolbar);
        toolbarGate.Visibility(annotating?ux::Visibility::Visible:ux::Visibility::Collapsed);
        find<controls::Primitives::ToggleButton>(header,L"Annotate").IsChecked(annotating);
        for(auto gate:{sidebarGate,toolbarGate}) {gate.HorizontalContentAlignment(ux::HorizontalAlignment::Stretch);gate.VerticalContentAlignment(ux::VerticalAlignment::Stretch);}
        attach(headerIsland, header); attach(sidebarIsland, sidebarGate); attach(toolbarIsland, toolbarGate,false); attach(helpIsland, help,false);
        attach(authIsland,authView);
#ifdef CALLAYERS_GENERATION
        auto ai=find<controls::Button>(sidebar,L"CreateAI");ai.IsEnabled(true);
#ifdef CALLAYERS_IMAGES
        auto import=find<controls::Button>(sidebar,L"ImportImage");import.IsEnabled(true);import.Click([this](auto&&,auto&&){actions.importImage();});
#endif
#ifndef CALLAYERS_LIBRARY
        controls::StackPanel form;form.Width(340);form.Spacing(10);
        controls::TextBlock label;label.Text(L"Create a card");label.FontSize(20);form.Children().Append(label);
        prompt=controls::TextBox();prompt.PlaceholderText(L"Describe your card…");prompt.AcceptsReturn(true);prompt.TextWrapping(ux::TextWrapping::Wrap);prompt.MinHeight(130);prompt.MaxLength(4000);form.Children().Append(prompt);
        generateButton=controls::Button();generateButton.Content(winrt::box_value(L"Create"));form.Children().Append(generateButton);
        cancelButton=controls::Button();cancelButton.Content(winrt::box_value(L"Cancel"));cancelButton.IsEnabled(false);form.Children().Append(cancelButton);
        generationMessage=controls::TextBlock();generationMessage.TextWrapping(ux::TextWrapping::Wrap);generationMessage.Text(L"Your allowance resets at midnight UTC.");form.Children().Append(generationMessage);
        controls::Flyout flyout;flyout.Content(form);ai.Flyout(flyout);
        generateButton.Click([this](auto&&,auto&&){actions.generate(std::wstring(prompt.Text()),{});});cancelButton.Click([this](auto&&,auto&&){actions.cancelGeneration();});
#else
        prompt=find<controls::TextBox>(sidebar,L"ComposerPrompt");generateButton=find<controls::Button>(sidebar,L"ComposerSend");cancelButton=find<controls::Button>(sidebar,L"ComposerCancel");generationMessage=find<controls::TextBlock>(sidebar,L"ComposerMessage");
        composerTitle=find<controls::TextBlock>(sidebar,L"ComposerTarget");composerDescription=find<controls::TextBlock>(sidebar,L"ComposerDescription");composerProgress=find<controls::ProgressRing>(sidebar,L"ComposerProgress");composerStatus=find<controls::ScrollViewer>(sidebar,L"ComposerStatus");
        attachmentPanel=find<controls::StackPanel>(sidebar,L"ComposerAttachments");
        ai.Click([this](auto&&,auto&&){openCreate();});generateButton.Click([this](auto&&,auto&&){sendMessage();});cancelButton.Click([this](auto&&,auto&&){actions.cancelGeneration();});
        prompt.TextChanged([this](auto&&,auto&&){if(!syncing)refreshComposer();});
        prompt.Paste([this](auto&&,controls::TextControlPasteEventArgs const& args){pasteText(args);});
        prompt.KeyDown([this](auto&&,ux::Input::KeyRoutedEventArgs const& args){if(args.Key()==winrt::Windows::System::VirtualKey::Enter && (GetKeyState(VK_CONTROL)&0x8000)){sendMessage();args.Handled(true);}});
        refreshComposer();
#endif
        generatedPanel=controls::StackPanel();generatedPanel.Spacing(6);find<controls::StackPanel>(sidebar,L"Rows").Children().InsertAt(0,generatedPanel);
#endif
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
        find<controls::Button>(sidebar,L"Name0").Content(winrt::box_value(L"Interactive card"));
        ux::Automation::AutomationProperties::SetName(find<controls::CheckBox>(sidebar,L"Show0"),L"Show Interactive card");
#endif
        headerIsland.SiteBridge().Hide();sidebarIsland.SiteBridge().Hide();toolbarIsland.SiteBridge().Hide();helpIsland.SiteBridge().Hide();
        StartupTrace("islands-ready");
        for(auto island:{headerIsland,sidebarIsland,toolbarIsland,helpIsland,authIsland}) {
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
        find<controls::Image>(authView,L"AuthLogo").Source(ux::Media::Imaging::BitmapImage(winrt::Windows::Foundation::Uri(L"file:///"+logo)));
        BOOL dark = TRUE; DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        find<controls::MenuFlyoutItem>(header,L"Quit").Click([this](auto&&,auto&&){actions.quit();});
        find<controls::MenuFlyoutItem>(header,L"SignOut").Click([this](auto&&,auto&&){actions.signOut();});
        auto aboutContent=load(LR"(<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Width="300" Spacing="12"><TextBlock Text="About Callayers" FontSize="20" FontWeight="SemiBold"/><TextBlock x:Name="Version" Foreground="#BBB9C8" IsTextSelectionEnabled="True"/><TextBlock x:Name="UpdateMessage" TextWrapping="Wrap"/><Button x:Name="Update" Content="Check for updates"/></StackPanel>)");
        find<controls::TextBlock>(aboutContent,L"Version").Text(std::wstring(L"Version ")+CALLAYERS_VERSION_TEXT);
        updateMessage=find<controls::TextBlock>(aboutContent,L"UpdateMessage");updateButton=find<controls::Button>(aboutContent,L"Update");
        about=controls::Flyout();about.Content(aboutContent);
        // These islands are only as tall as their toolbars. Let the popovers
        // measure against the monitor work area so all their content fits.
        about.ShouldConstrainToRootBounds(false);
        find<controls::Button>(toolbar,L"Color").Flyout().ShouldConstrainToRootBounds(false);
        about.Closed([this](auto&&,auto&&){aboutOpen=false;});
        showUpdate(ReleaseRoot().empty()?L"development":L"idle");
        find<controls::MenuFlyoutItem>(header,L"About").Click([this](auto&&,auto&&){aboutOpen=true;updatePoll=0;pollUpdate();about.ShowAt(find<controls::Button>(header,L"File"));});
        updateButton.Click([this](auto&&,auto&&){if(updateReady){if(actions.restartUpdate())about.Hide();else showUpdate(L"waiting-for-camera");}else checkUpdate();});
        if(!ReleaseRoot().empty())checkUpdate();
        find<controls::Button>(authView,L"AuthPrimary").Click([this](auto&&,auto&&){actions.authPrimary();});
        find<controls::Button>(authView,L"AuthCancel").Click([this](auto&&,auto&&){actions.authCancel();});
        find<controls::Primitives::ToggleButton>(header,L"Annotate").Click([this](auto&& sender,auto&&){annotating = sender.template as<controls::Primitives::ToggleButton>().IsChecked().GetBoolean();if(!annotating)actions.tool(AnnotationTool::Select);actions.layout();SaveAnnotationBarOpen(annotating);});
        auto cameraChange = [this](auto&& sender,auto&&) { if (!syncing) actions.camera(sender.template as<controls::ComboBox>().SelectedIndex()); };
        find<controls::ComboBox>(header,L"Camera").SelectionChanged(cameraChange);
        find<controls::ComboBox>(help,L"HelpCamera").SelectionChanged(cameraChange);
        find<controls::Button>(help,L"Retry").Click([this](auto&&,auto&&){actions.retry();});
        find<controls::Button>(header,L"RetryChoice").Click([this](auto&&,auto&&){actions.retry();});
        find<controls::Button>(help,L"Privacy").Click([this](auto&&,auto&&){actions.privacy();});
        find<controls::Button>(sidebar,L"HideAll").Click([this](auto&&,auto&&){actions.hideAll();});
#ifndef CALLAYERS_LIBRARY
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
#ifdef CALLAYERS_GENERATION
            for(size_t i=0;i<generatedNames.size();++i){auto name=generatedNames[i];std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});bool show=name.find(query)!=std::wstring::npos;any|=show;generatedRows[i].Visibility(show?ux::Visibility::Visible:ux::Visibility::Collapsed);}
            find<controls::TextBlock>(sidebar,L"NoResults").Visibility(any?ux::Visibility::Collapsed:ux::Visibility::Visible);
#endif
        });
#else
        find<controls::TextBox>(sidebar,L"Search").TextChanged([this](auto&&,auto&&){filterLibrary();});
#endif
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
    ~Impl() { lifetime.reset();if(updateProcess)CloseHandle(updateProcess);if(about)about.Hide();editor.close();authIsland.Close();helpIsland.Close(); toolbarIsland.Close(); sidebarIsland.Close(); headerIsland.Close(); }
};
Workspace::Workspace(HWND window,WorkspaceActions actions) : impl_(std::make_unique<Impl>(window,std::move(actions))) {}
Workspace::~Workspace() = default;
void Workspace::refreshUpdate(){impl_->pollUpdate();}
int Workspace::toolbarHeight() const { return impl_->authorized && impl_->annotating?56:0; }
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
    if(selected<3 && ui.selected==selected && std::equal(visible,visible+3,ui.listedVisible))return;
    ui.selected=selected;std::copy(visible,visible+3,ui.listedVisible);ui.syncing=true;
    for(int i=0;i<3;++i) {
        auto number=std::to_wstring(i);
        ui.find<controls::CheckBox>(ui.sidebar,(L"Show"+number).c_str()).IsChecked(visible[i]);
        ui.find<controls::Border>(ui.sidebar,(L"Row"+number).c_str()).Background(ux::Media::SolidColorBrush(selected==i?winrt::Windows::UI::Color{255,59,57,83}:winrt::Windows::UI::Color{0,0,0,0}));
    }
    std::wstring name=selected>=0 && selected<3?Impl::names[selected]:L"Select a card";
#ifdef CALLAYERS_GENERATION
    if(selected>=3 && selected-3<int(ui.generatedNames.size()))name=ui.generatedNames[selected-3];
    for(size_t i=0;i<ui.generatedRows.size();++i)ui.generatedRows[i].Background(ux::Media::SolidColorBrush(selected==int(i+3)?winrt::Windows::UI::Color{255,59,57,83}:winrt::Windows::UI::Color{0,0,0,0}));
#endif
    ui.find<controls::TextBlock>(ui.sidebar,L"Selection").Text(name);
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
#ifdef CALLAYERS_GENERATION
void Workspace::generation(const GenerationSnapshot& state){auto& ui=*impl_;auto message=state.message;
    if(state.remaining>=0){
#ifdef CALLAYERS_LIBRARY
        message+=(message.empty()?L"":L"\n")+std::to_wstring(state.remaining)+L" left today · resets 00:00 UTC";
#else
        message+=L"\n"+std::to_wstring(state.remaining)+L" generations remaining today. Resets at midnight UTC.";
#endif
    }
    if(message!=ui.lastGenerationMessage){
#ifdef CALLAYERS_LIBRARY
        ui.showComposerMessage(message);
#else
        ui.generationMessage.Text(message);
#endif
        ui.lastGenerationMessage=message;
    }
#ifdef CALLAYERS_LIBRARY
    bool finished=ui.lastGenerating && !state.busy;ui.lastGenerating=state.busy;
    if(finished){
        if((state.message==L"Card saved." || state.message.find(L"Card created.")==0) && std::wstring(ui.prompt.Text())==ui.submittedPrompt)ui.clearDraft();
        if(ui.editingId!=ui.selectedId){ui.editingId=ui.selectedId;ui.clearDraft();}
    }
    ui.cancelButton.IsEnabled(!state.applying);ui.refreshComposer();
    if(state.applying)ui.composerDescription.Text(state.message);
#else
    ui.generateButton.IsEnabled(!state.busy);ui.cancelButton.IsEnabled(state.busy);
#endif
}
void Workspace::generatedCards(const std::vector<std::pair<std::wstring,bool>>& cards){auto& ui=*impl_;
    bool rebuild=cards.size()!=ui.generatedNames.size();if(!rebuild)for(size_t i=0;i<cards.size();++i)rebuild|=cards[i].first!=ui.generatedNames[i];
    ui.syncing=true;
    if(rebuild){ui.generatedPanel.Children().Clear();ui.generatedNames.clear();ui.generatedChecks.clear();ui.generatedRows.clear();
        for(size_t i=0;i<cards.size();++i){ui.generatedNames.push_back(cards[i].first);controls::Border row;row.CornerRadius(ux::CornerRadius{6});row.Padding(ux::Thickness{4});
            controls::StackPanel content;content.Orientation(controls::Orientation::Horizontal);controls::CheckBox check;check.IsChecked(cards[i].second);
            ux::Automation::AutomationProperties::SetName(check,L"Show "+cards[i].first);content.Children().Append(check);
            controls::Button name;name.Content(winrt::box_value(cards[i].first));content.Children().Append(name);row.Child(content);
            check.Click([&ui,index=int(i+3)](auto&& sender,auto&&){if(!ui.syncing)ui.actions.visibility(index,sender.template as<controls::CheckBox>().IsChecked().GetBoolean());});
            name.Click([&ui,index=int(i+3)](auto&&,auto&&){ui.actions.selectCard(index);});ui.generatedChecks.push_back(check);ui.generatedRows.push_back(row);
        }
        for(auto it=ui.generatedRows.rbegin();it!=ui.generatedRows.rend();++it)ui.generatedPanel.Children().Append(*it);
    }else for(size_t i=0;i<cards.size();++i)if(ui.generatedChecks[i].IsChecked().GetBoolean()!=cards[i].second)ui.generatedChecks[i].IsChecked(cards[i].second);
    std::wstring query(ui.find<controls::TextBox>(ui.sidebar,L"Search").Text());std::transform(query.begin(),query.end(),query.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
    bool any=false;for(int i=0;i<3;++i)any|=ui.find<controls::Border>(ui.sidebar,(L"Row"+std::to_wstring(i)).c_str()).Visibility()==ux::Visibility::Visible;
    for(size_t i=0;i<cards.size();++i){auto name=cards[i].first;std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});bool show=name.find(query)!=std::wstring::npos;any|=show;ui.generatedRows[i].Visibility(show?ux::Visibility::Visible:ux::Visibility::Collapsed);}
    ui.find<controls::TextBlock>(ui.sidebar,L"NoResults").Visibility(any?ux::Visibility::Collapsed:ux::Visibility::Visible);
    ui.syncing=false;
}
#ifdef CALLAYERS_LIBRARY
void Workspace::libraryCards(const std::vector<GeneratedCard>& cards,int selected){auto& ui=*impl_;
    bool rebuild=cards.size()!=ui.library.size();if(!rebuild)for(size_t i=0;i<cards.size();++i)rebuild|=cards[i].id!=ui.library[i].id || cards[i].name!=ui.library[i].name || cards[i].previous.has_value()!=ui.library[i].previous || cards[i].edited!=ui.library[i].edited;
    ui.syncing=true;
    if(rebuild){ui.generatedPanel.Children().Clear();ui.library.clear();ui.generatedRows.clear();ui.generatedChecks.clear();ui.libraryRenameFields.clear();ui.libraryNameLabels.clear();
        for(size_t i=0;i<cards.size();++i){const auto& card=cards[i];ui.library.push_back({card.id,card.name,card.previous.has_value(),card.edited});
            controls::Border row;row.CornerRadius(ux::CornerRadius{6});row.Padding(ux::Thickness{4,2,4,2});controls::Grid grid;
            controls::ColumnDefinition checkColumn;checkColumn.Width(ux::GridLength{36,ux::GridUnitType::Pixel});grid.ColumnDefinitions().Append(checkColumn);
            controls::ColumnDefinition nameColumn;nameColumn.Width(ux::GridLength{1,ux::GridUnitType::Star});grid.ColumnDefinitions().Append(nameColumn);
            controls::ColumnDefinition menuColumn;menuColumn.Width(ux::GridLength{32,ux::GridUnitType::Pixel});grid.ColumnDefinitions().Append(menuColumn);
            controls::CheckBox check;check.MinWidth(32);check.Width(32);check.IsChecked(card.visible);ux::Automation::AutomationProperties::SetName(check,L"Show "+card.name);grid.Children().Append(check);
            controls::ContentControl nameTarget;nameTarget.IsTabStop(true);nameTarget.HorizontalContentAlignment(ux::HorizontalAlignment::Stretch);nameTarget.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{0,0,0,0}));controls::Grid::SetColumn(nameTarget,1);grid.Children().Append(nameTarget);
            controls::Grid titleArea;titleArea.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{0,0,0,0}));nameTarget.Content(titleArea);controls::TextBlock label;label.Text(card.name);label.TextTrimming(ux::TextTrimming::CharacterEllipsis);label.TextWrapping(ux::TextWrapping::NoWrap);label.Padding(ux::Thickness{6,8,6,8});label.VerticalAlignment(ux::VerticalAlignment::Center);titleArea.Children().Append(label);
            controls::ToolTipService::SetToolTip(nameTarget,winrt::box_value(card.name+L"\nDouble-click to rename"));ux::Automation::AutomationProperties::SetName(nameTarget,L"Select "+card.name);
            controls::TextBox rename;rename.Text(card.name);rename.MaxLength(80);rename.MinWidth(0);rename.Padding(ux::Thickness{4});rename.Visibility(ux::Visibility::Collapsed);ux::Automation::AutomationProperties::SetName(rename,L"Card name");titleArea.Children().Append(rename);
            ui.libraryRenameFields.push_back(rename);ui.libraryNameLabels.push_back(label);
            auto renaming=std::make_shared<bool>(false);auto renameFocused=std::make_shared<bool>(false);auto weakRename=winrt::make_weak(rename);auto weakLabel=winrt::make_weak(label);auto id=card.id;
            auto finishRename=[&ui,weakRename,weakLabel,renaming,renameFocused,id](bool save){if(!*renaming)return;*renaming=false;*renameFocused=false;Resources().event(save?"card_rename_finished_save":"card_rename_finished_cancel");auto field=weakRename.get();auto text=weakLabel.get();if(!field || !text)return;auto value=std::wstring(field.Text());field.Visibility(ux::Visibility::Collapsed);text.Visibility(ux::Visibility::Visible);if(save && !ui.syncing && value!=std::wstring(text.Text()))ui.actions.renameCard(id,value);};
            auto weakLifetime=std::weak_ptr<int>(ui.lifetime);
            auto beginRename=[&ui,weakLifetime,weakRename,weakLabel,renaming,renameFocused,id]{
                if(weakLifetime.expired())return;
                Resources().event("card_rename_requested");
                // Let routed input finish before taking focus; otherwise its default
                // focus handling can immediately dismiss the newly opened editor.
                bool queued=ui.sidebar.DispatcherQueue().TryEnqueue([&ui,weakLifetime,weakRename,weakLabel,renaming,renameFocused,id]{
                    if(weakLifetime.expired() || *renaming)return;
                    for(size_t j=0;j<ui.library.size();++j)if(ui.library[j].id==id){ui.actions.selectCard(int(j));break;}
                    auto field=weakRename.get();auto text=weakLabel.get();if(!field || !text){Resources().event("card_rename_controls_unavailable");return;}
                    field.Text(text.Text());*renaming=true;*renameFocused=false;
                    text.Visibility(ux::Visibility::Collapsed);field.Visibility(ux::Visibility::Visible);
                    field.UpdateLayout();bool focused=field.Focus(ux::FocusState::Programmatic);field.SelectAll();
                    Resources().event(focused?"card_rename_opened_focused":"card_rename_opened_without_focus");
                });
                if(!queued)Resources().event("card_rename_dispatch_failed");
            };
            nameTarget.Tapped([&ui,id,renaming](auto&&,ux::Input::TappedRoutedEventArgs const& args){if(*renaming)return;for(size_t j=0;j<ui.library.size();++j)if(ui.library[j].id==id){ui.actions.selectCard(int(j));break;}args.Handled(true);});
            nameTarget.IsDoubleTapEnabled(true);nameTarget.DoubleTapped([beginRename,renaming](auto&&,ux::Input::DoubleTappedRoutedEventArgs const& args){Resources().event("card_rename_double_tapped");if(!*renaming){beginRename();args.Handled(true);}});
            nameTarget.KeyDown([&ui,id,beginRename,renaming](auto&&,ux::Input::KeyRoutedEventArgs const& args){if(*renaming)return;if(args.Key()==winrt::Windows::System::VirtualKey::F2){beginRename();args.Handled(true);}else if(args.Key()==winrt::Windows::System::VirtualKey::Enter || args.Key()==winrt::Windows::System::VirtualKey::Space){ui.openEdit(id);args.Handled(true);}});
            rename.KeyDown([finishRename](auto&&,ux::Input::KeyRoutedEventArgs const& args){if(args.Key()==winrt::Windows::System::VirtualKey::Enter || args.Key()==winrt::Windows::System::VirtualKey::Escape){finishRename(args.Key()==winrt::Windows::System::VirtualKey::Enter);args.Handled(true);}});
            rename.GotFocus([renaming,renameFocused](auto&&,auto&&){if(*renaming)*renameFocused=true;});
            rename.LostFocus([finishRename,renameFocused](auto&&,auto&&){if(*renameFocused)finishRename(true);});
            controls::Button menu;menu.Content(winrt::box_value(L"⋯"));menu.Padding(ux::Thickness{4});menu.Background(ux::Media::SolidColorBrush(winrt::Windows::UI::Color{0,0,0,0}));menu.BorderThickness(ux::Thickness{0});controls::Grid::SetColumn(menu,2);ux::Automation::AutomationProperties::SetName(menu,L"Actions for "+card.name);grid.Children().Append(menu);controls::MenuFlyout flyout;
            auto item=[&](const wchar_t* text,std::function<void()> action){controls::MenuFlyoutItem entry;entry.Text(text);entry.Click([action](auto&&,auto&&){action();});flyout.Items().Append(entry);};
            item(L"Edit",[&ui,id]{ui.openEdit(id);});
            auto renameAfterClose=std::make_shared<bool>(false);
            item(L"Rename",[renameAfterClose]{Resources().event("card_rename_menu_clicked");*renameAfterClose=true;});
            // The flyout restores focus to its button as it closes. Open the
            // editor only afterwards so that restoration cannot commit it.
            flyout.Closed([beginRename,renameAfterClose](auto&&,auto&&){if(*renameAfterClose){*renameAfterClose=false;beginRename();}});
            item(L"Bring to front",[&ui,id]{ui.actions.stackCard(id,true);});item(L"Send to back",[&ui,id]{ui.actions.stackCard(id,false);});
            if(card.previous)item(L"Restore previous version",[&ui,id]{ui.actions.restoreCard(id);});flyout.Items().Append(controls::MenuFlyoutSeparator());item(L"Delete",[&ui,id]{ui.actions.deleteCard(id);});menu.Flyout(flyout);
            check.Click([&ui,id](auto&& sender,auto&&){if(ui.syncing)return;for(size_t j=0;j<ui.library.size();++j)if(ui.library[j].id==id){ui.actions.visibility(int(j),sender.template as<controls::CheckBox>().IsChecked().GetBoolean());return;}});
            row.Child(grid);ui.generatedRows.push_back(row);ui.generatedChecks.push_back(check);
        }
        std::vector<size_t> order;for(size_t i=0;i<cards.size();++i)order.push_back(i);std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return cards[a].edited==cards[b].edited?a>b:cards[a].edited>cards[b].edited;});for(auto i:order)ui.generatedPanel.Children().Append(ui.generatedRows[i]);
    }
    for(size_t i=0;i<cards.size();++i){if(ui.generatedChecks[i].IsChecked().GetBoolean()!=cards[i].visible)ui.generatedChecks[i].IsChecked(cards[i].visible);ui.generatedRows[i].Background(ux::Media::SolidColorBrush(selected==int(i)?winrt::Windows::UI::Color{255,59,57,83}:winrt::Windows::UI::Color{0,0,0,0}));}
    std::wstring id=selected>=0 && selected<int(cards.size())?cards[selected].id:L"";
    if(id!=ui.selectedId){ui.selectedId=id;if(!ui.lastGenerating){ui.editingId=id;ui.clearDraft();ui.showComposerMessage(L"");}}
    ui.refreshComposer();
    ui.filterLibrary();ui.syncing=false;
}
#endif
#endif
bool Workspace::editingText() const {return impl_->editor.active();}
bool Workspace::textEditorOwns(HWND window) const {return impl_->editor.owns(window);}
bool Workspace::finishText(bool commit) {
    auto& ui=*impl_;if(!ui.editor.active())return true;if(ui.finishingText)return false;
    ui.finishingText=true;
    auto text=ui.editor.text();
    bool accepted=ui.actions.text(std::move(text),commit && !ui.editor.unavailable(),ui.editor.height());
    if(accepted){++ui.editorToken;ui.editor.close();}
    ui.finishingText=false;
    if(!accepted && ui.editor.active())ui.editor.focus();
    return accepted;
}
void Workspace::editText(ultralight::Renderer* renderer,const RECT& area,const std::wstring& value,double font,const std::wstring& color,double width,double scale,double minimumHeight,bool waitForBackdrop) {
    auto& ui=*impl_;if(!finishText())return;
    auto token=++ui.editorToken;auto weak=std::weak_ptr<int>(ui.lifetime);
    ui.editor.open(renderer,ui.window,area,value,font,color,width,scale,minimumHeight,waitForBackdrop,[this,weak,token](bool commit){
        if(weak.expired())return;
        impl_->header.DispatcherQueue().TryEnqueue([this,weak,token,commit]{
            if(weak.expired() || impl_->editorToken!=token)return;
            bool unavailable=impl_->editor.unavailable();
            if(impl_->editor.active())finishText(commit);
            if(unavailable){Resources().event("text_editor_open_failed");MessageBoxW(impl_->window,L"Couldn't open text editing. Try again.",L"Text editing",MB_OK|MB_ICONINFORMATION);}
        });
    });
}
void Workspace::textBackdrop(bool clean){impl_->editor.backdrop(clean);}
void Workspace::focus(bool reverse) {
    auto order=impl_->focusOrder();impl_->focusTarget(reverse?order.back():order.front(),reverse);
}
void Workspace::state(const std::wstring& status,const std::wstring& message,bool fresh,bool privacy,bool retryPreference,bool cameraChoices) {
    auto& ui=*impl_;
    if(ui.lastStatus!=status){ui.find<controls::TextBlock>(ui.header,L"Status").Text(status);ui.lastStatus=status;}
    if(ui.lastHelp!=message){ui.find<controls::TextBlock>(ui.help,L"Help").Text(message);ui.lastHelp=message;}
    if(ui.lastCameraChoices!=cameraChoices){
        auto visibility=cameraChoices?ux::Visibility::Visible:ux::Visibility::Collapsed;
        ui.find<controls::ComboBox>(ui.help,L"HelpCamera").Visibility(visibility);
        ui.find<controls::StackPanel>(ui.help,L"HelpActions").Visibility(visibility);
        ui.lastCameraChoices=cameraChoices;
    }
    if(ui.lastPrivacy!=privacy){ui.find<controls::Button>(ui.help,L"Privacy").Visibility(privacy?ux::Visibility::Visible:ux::Visibility::Collapsed);ui.lastPrivacy=privacy;}
    if(ui.lastRetryPreference!=retryPreference){ui.find<controls::Button>(ui.header,L"RetryChoice").Visibility(retryPreference?ux::Visibility::Visible:ux::Visibility::Collapsed);ui.lastRetryPreference=retryPreference;}
    if(ui.lastFresh!=fresh){
        ui.sidebarGate.IsEnabled(fresh); ui.toolbarGate.IsEnabled(fresh);
        if(fresh || !ui.authorized)ui.helpIsland.SiteBridge().Hide();else ui.helpIsland.SiteBridge().Show();ui.lastFresh=fresh;
    }
}
void Workspace::authentication(const AuthSnapshot& state) {
    auto& ui=*impl_;ui.authorized=state.allowed;
    ui.find<controls::MenuFlyoutItem>(ui.header,L"Account").Text(state.email.empty()?L"Signed in":state.email);
    ui.find<controls::TextBlock>(ui.authView,L"AuthMessage").Text(state.message.empty()?L"Sign in to use Callayers.":state.message);
    bool busy=state.phase==AuthPhase::Checking || state.phase==AuthPhase::SigningIn;
    ui.find<controls::ProgressRing>(ui.authView,L"AuthProgress").IsActive(busy);
    ui.find<controls::ProgressRing>(ui.authView,L"AuthProgress").Visibility(busy?ux::Visibility::Visible:ux::Visibility::Collapsed);
    ui.find<controls::Button>(ui.authView,L"AuthPrimary").Visibility(busy?ux::Visibility::Collapsed:ux::Visibility::Visible);
    ui.find<controls::Button>(ui.authView,L"AuthPrimary").Content(winrt::box_value(state.retryAvailable?L"Retry":L"Sign in with Google"));
    ui.find<controls::Button>(ui.authView,L"AuthCancel").Visibility(busy?ux::Visibility::Visible:ux::Visibility::Collapsed);
    if(state.allowed){ui.authIsland.SiteBridge().Hide();ui.headerIsland.SiteBridge().Show();ui.sidebarIsland.SiteBridge().Show();if(ui.annotating)ui.toolbarIsland.SiteBridge().Show();if(!ui.lastFresh)ui.helpIsland.SiteBridge().Show();}
    else {ui.headerIsland.SiteBridge().Hide();ui.sidebarIsland.SiteBridge().Hide();ui.toolbarIsland.SiteBridge().Hide();ui.helpIsland.SiteBridge().Hide();ui.authIsland.SiteBridge().Show();}
}
void Workspace::layout(int width,int height,float dpi,const RECT& preview) {
    auto& ui=*impl_; int header=int(72*dpi), sidebar=int(300*dpi), toolbar=int(toolbarHeight()*dpi);
    ui.headerIsland.SiteBridge().MoveAndResize({0,0,width,header});
    ui.authIsland.SiteBridge().MoveAndResize({0,0,width,height});
    ui.sidebarIsland.SiteBridge().MoveAndResize({0,header,sidebar,std::max(1,height-header)});
    ui.toolbarIsland.SiteBridge().MoveAndResize({sidebar,header,std::max(1,width-sidebar),std::max(1,toolbar)});
    ui.toolbarGate.Visibility(ui.annotating?ux::Visibility::Visible:ux::Visibility::Collapsed);
    ui.find<controls::TextBlock>(ui.toolbar,L"ToolHelp").Visibility(width/dpi>=1450?ux::Visibility::Visible:ux::Visibility::Collapsed);
    if(toolbar && ui.authorized)ui.toolbarIsland.SiteBridge().Show();else ui.toolbarIsland.SiteBridge().Hide();
    ui.helpIsland.SiteBridge().MoveAndResize({preview.left,preview.top,std::max(1L,preview.right-preview.left),std::max(1L,preview.bottom-preview.top)});
}
}
