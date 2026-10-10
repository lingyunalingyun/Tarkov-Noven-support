#ifndef Payload
  #error Payload must come from audited cmake install staging
#endif
#ifndef AppVersion
  #error AppVersion must come from CMake project version
#endif
#ifndef VcRedist
  #error An authentic Microsoft VC++ x64 prerequisite is required
#endif
#define BundleVersionMS 0
#define BundleVersionLS 0
#expr GetVersionNumbers(AddBackslash(Payload) + "NovenLauncher.exe", BundleVersionMS, BundleVersionLS)
[Setup]
AppId={{70C934D2-C53B-4F49-A8C7-152E748A8E54}
AppName=Noven Tarkov Support
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}.0
VersionInfoProductVersion={#AppVersion}
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
AppMutex=Local\NovenTarkovSupport.App.70C934D2,Local\NovenTarkovSupport.Host.70C934D2,Local\NovenTarkovSupport.Launcher.70C934D2,Local\NovenTarkovSupport.Updater.70C934D2,Local\NovenTarkovSupport.Installer.70C934D2
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\NovenLauncher.exe
; 用户数据不属于安装清单：升级和卸载均不得清理。
; User data is not an installed file: neither upgrade nor uninstall removes it.
[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked
[Files]
; 只解包到固定安装暂存树；Updater 验证完整清单后提交，不直接覆盖活动版本。
; Extract only to fixed installer staging; Updater commits a verified inventory without patching the active version.
Source: "{#Payload}\versions\{#AppVersion}\*"; DestDir: "{app}\installer-staging\{#AppVersion}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\initial.json"; DestDir: "{app}\installer-staging"; DestName: "inventory.json"; Flags: ignoreversion
Source: "{#Payload}\NovenLauncher.exe"; DestDir: "{app}"; Flags: replacesameversion
Source: "{#Payload}\NovenUpdater.exe"; DestDir: "{app}"; Flags: replacesameversion
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
var Code: Integer; ProgramRoot, UserRoot: String; InstalledMS, InstalledLS: Cardinal;
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
  if GetVersionNumbers(ExpandConstant('{app}\NovenLauncher.exe'), InstalledMS, InstalledLS) then begin
    if (InstalledMS > {#BundleVersionMS}) or ((InstalledMS = {#BundleVersionMS}) and (InstalledLS > {#BundleVersionLS})) then begin
      Result := 'A newer Noven bootstrap is installed. Use its installer to repair; application rollback is an explicit Settings action.'; exit;
    end;
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
procedure CurStepChanged(CurStep: TSetupStep);
var Code: Integer;
begin
  if CurStep = ssInstall then begin
    CreateMutex('Local\NovenTarkovSupport.Installer.70C934D2');
    if CheckForMutexes('Local\NovenTarkovSupport.App.70C934D2,Local\NovenTarkovSupport.Host.70C934D2,Local\NovenTarkovSupport.Launcher.70C934D2,Local\NovenTarkovSupport.Updater.70C934D2') then
      RaiseException('Close Noven normally before installation. No process will be forcibly terminated.');
  end;
  if CurStep = ssPostInstall then begin
    if not Exec(ExpandConstant('{app}\NovenUpdater.exe'), '--install-bundle', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) then
      RaiseException('Could not start the trusted Noven installer commit. Repair with this installer.');
    if Code <> 0 then RaiseException('Noven Core validation/commit failed. Previous application and user data are retained; repair with this installer.');
  end;
end;
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    CreateMutex('Local\NovenTarkovSupport.Installer.70C934D2');
    if CheckForMutexes('Local\NovenTarkovSupport.App.70C934D2,Local\NovenTarkovSupport.Host.70C934D2,Local\NovenTarkovSupport.Launcher.70C934D2,Local\NovenTarkovSupport.Updater.70C934D2') then
      RaiseException('Close Noven normally before uninstalling. User data will be retained.');
  end;
end;
