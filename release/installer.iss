#ifndef ReleaseVersion
  #error ReleaseVersion is required
#endif
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef RuntimeInstaller
  #error RuntimeInstaller is required
#endif
#ifndef PublicKey
  #error PublicKey is required
#endif

[Setup]
AppId={{7CC8E8B2-7493-43C5-9B0F-301E5501E067}
AppName=Callayers
AppVersion={#ReleaseVersion}
AppPublisher=Callayers
AppPublisherURL=https://callayers.com
DefaultDirName={localappdata}\Programs\Callayers
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=CallayersSetup-{#ReleaseVersion}
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
AppMutex=Local\Callayers.Foundation.v2.Desktop
UninstallDisplayIcon={app}\Callayers.exe
SetupIconFile=..\spec-v2\assets\callayers.ico
WizardStyle=modern

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}\versions\{#ReleaseVersion}"; Excludes: "*Tests.exe,RendererSecurityProbe.exe,CallayersLauncher.exe,*.pdb,*.lib,*.exp,diagnostics\*"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#PayloadDir}\CallayersLauncher.exe"; DestDir: "{app}"; DestName: "Callayers.exe"; Flags: ignoreversion
Source: "update.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "update-common.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublicKey}"; DestDir: "{app}"; DestName: "update-public-key.json"; Flags: ignoreversion
Source: "{#RuntimeInstaller}"; DestName: "WindowsAppRuntimeInstall.exe"; Flags: dontcopy

[Icons]
Name: "{userprograms}\Callayers"; Filename: "{app}\Callayers.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\Callayers.exe"; Description: "Open Callayers"; Flags: postinstall nowait skipifsilent

[Code]
function CameraIdle: Boolean;
var OldVersion: AnsiString; Handle: Integer; Camera: String;
begin
  Result := True;
  if LoadStringFromFile(ExpandConstant('{app}\current-version.txt'), OldVersion) then begin
    Camera := ExpandConstant('{app}\versions\') + Trim(String(OldVersion)) + '\CallayersCamera.dll';
    if FileExists(Camera) then begin
      Handle := FileOpen(Camera, fmOpenReadWrite or fmShareExclusive);
      Result := Handle <> -1;
      if Result then FileClose(Handle);
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer; Shell: String;
begin
  Result := '';
  if not CameraIdle then begin
    Result := 'Close the calling app or browser using the Callayers camera, then try again.';
    exit;
  end;
  Shell := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  if Exec(Shell, '-NoProfile -NonInteractive -Command "if (Get-AppxPackage Microsoft.WindowsAppRuntime.1.6) { exit 0 } else { exit 1 }"', '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0) then exit;
  ExtractTemporaryFile('WindowsAppRuntimeInstall.exe');
  if not Exec(ExpandConstant('{tmp}\WindowsAppRuntimeInstall.exe'), '--quiet', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then begin
    Result := 'Windows could not install the required runtime for your account. Ask your administrator to install Windows App Runtime 1.6, then run this installer again.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Code: Integer; Camera: String; Pointer: String;
begin
  if CurStep = ssPostInstall then begin
    Camera := ExpandConstant('{app}\versions\{#ReleaseVersion}\CallayersCamera.dll');
    if not Exec(ExpandConstant('{sys}\regsvr32.exe'), '/s "' + Camera + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
      RaiseException('Camera registration failed. Your previous version remains selected.');
    Pointer := ExpandConstant('{app}\current-version.txt');
    if not SaveStringToFile(Pointer + '.tmp', '{#ReleaseVersion}', False) then RaiseException('Could not select the installed version.');
    // Replace the tiny version pointer only after registration succeeds.
    if FileExists(Pointer) then begin
      if not FileCopy(Pointer, Pointer + '.previous', False) then RaiseException('Could not preserve the previous version.');
      if not DeleteFile(Pointer) then RaiseException('Could not select the installed version.');
    end;
    if not RenameFile(Pointer + '.tmp', Pointer) then begin
      FileCopy(Pointer + '.previous', Pointer, False);
      RaiseException('Could not select the installed version.');
    end;
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := CameraIdle;
  if not Result then MsgBox('Close the calling app or browser using the Callayers camera, then uninstall again.', mbInformation, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Camera: String; Code: Integer;
begin
  if CurUninstallStep = usUninstall then begin
    if RegQueryStringValue(HKCU, 'Software\Classes\CLSID\{49C2DCA8-F030-4C75-9B0E-C51E33C1A291}\InprocServer32', '', Camera) then begin
      if Pos(Lowercase(ExpandConstant('{app}\versions\')), Lowercase(Camera)) = 1 then
        Exec(ExpandConstant('{sys}\regsvr32.exe'), '/s /u "' + Camera + '"', '', SW_HIDE, ewWaitUntilTerminated, Code);
    end;
  end;
end;

[UninstallDelete]
Type: files; Name: "{app}\current-version.txt"
Type: files; Name: "{app}\current-version.txt.previous"
Type: files; Name: "{app}\update-status.json"
