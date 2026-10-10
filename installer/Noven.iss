#ifndef Payload
  #error Payload must come from audited cmake install staging
#endif
#ifndef AppVersion
  #error AppVersion must come from CMake project version
#endif
#ifndef VcRedist
  #error An authentic Microsoft VC++ x64 prerequisite is required
#endif
[Setup]
AppId={{70C934D2-C53B-4F49-A8C7-152E748A8E54}
AppName=Noven Tarkov Support
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}.0
DefaultDirName={localappdata}\Programs\Noven Tarkov Support
DisableDirPage=yes
DefaultGroupName=Noven Tarkov Support
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#ArtifactDir}
OutputBaseFilename=NovenTarkovSupport-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
AppMutex=Local\NovenTarkovSupport.App.70C934D2,Local\NovenTarkovSupport.Host.70C934D2,Local\NovenTarkovSupport.Launcher.70C934D2,Local\NovenTarkovSupport.Updater.70C934D2
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\NovenLauncher.exe
; 用户数据不属于安装清单：升级和卸载均不得清理。
; User data is not an installed file: neither upgrade nor uninstall removes it.
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked
[Files]
Source: "{#Payload}\versions\*"; DestDir: "{app}\versions"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\NovenLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\NovenUpdater.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\initial.json"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "{#Payload}\current.json"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "{#Payload}\last-good.json"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "{#VcRedist}"; DestName: "vc_redist.x64.exe"; Flags: dontcopy
[Icons]
Name: "{userprograms}\Noven Tarkov Support\Noven Tarkov Support"; Filename: "{app}\NovenLauncher.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\Noven Tarkov Support"; Filename: "{app}\NovenLauncher.exe"; WorkingDir: "{app}"; Tasks: desktopicon
[Run]
Filename: "{app}\NovenLauncher.exe"; Description: "Launch Noven Tarkov Support"; Flags: nowait postinstall skipifsilent
[UninstallRun]
Filename: "{app}\NovenUpdater.exe"; Parameters: "--remove-versions"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveTrustedApplicationVersions"
[Code]
function RuntimeReady: Boolean;
var Installed, Major, Minor, Build: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1);
  if not Result then exit;
  Result := RegQueryDWordValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Major', Major) and
    RegQueryDWordValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Minor', Minor) and
    RegQueryDWordValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Bld', Build);
  Result := Result and ((Major > {#VcMajor}) or ((Major = {#VcMajor}) and ((Minor > {#VcMinor}) or ((Minor = {#VcMinor}) and (Build >= {#VcBuild})))));
end;
function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer; ProgramRoot, UserRoot: String;
begin
  Result := '';
  ProgramRoot := AddBackslash(ExpandFileName(ExpandConstant('{app}')));
  UserRoot := AddBackslash(ExpandFileName(ExpandConstant('{localappdata}\Noven Tarkov Support')));
  if PathStartsWith(ProgramRoot, UserRoot, True) or PathStartsWith(UserRoot, ProgramRoot, True) then begin
    Result := 'Program and user data directories must not overlap.'; exit;
  end;
  if CheckForMutexes('Local\NovenTarkovSupport.App.70C934D2,Local\NovenTarkovSupport.Host.70C934D2,Local\NovenTarkovSupport.Launcher.70C934D2,Local\NovenTarkovSupport.Updater.70C934D2') then begin
    Result := 'Close Noven and its PluginHost normally before installing. No process will be forcibly terminated.'; exit;
  end;
  if RuntimeReady then exit;
  Result := 'Microsoft Visual C++ x64 Runtime {#VcMajor}.{#VcMinor}.{#VcBuild} or newer is required. The prerequisite may require administrator approval; Noven itself installs per-user.';
  if WizardSilent then exit;
  if MsgBox(Result + #13#10 + 'Install the bundled Microsoft prerequisite now?', mbConfirmation, MB_YESNO) <> IDYES then exit;
  ExtractTemporaryFile('vc_redist.x64.exe');
  if ShellExec('open', ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /passive /norestart', '', SW_SHOW, ewWaitUntilTerminated, Code) then begin
    if RuntimeReady then Result := '';
    if Code = 3010 then NeedsRestart := True;
  end;
end;
