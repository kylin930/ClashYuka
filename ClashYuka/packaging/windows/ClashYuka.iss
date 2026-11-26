; ClashYuka Inno Setup Installer Script (Phase 11 Task 30)
;
; Build with:
;   iscc "packaging\windows\ClashYuka.iss"
;
; Expects the following layout relative to this script's parent directory:
;   ..\build\Release\ClashYuka.exe      (main executable)
;   ..\resources\                          (mihomo.exe, Country.mmdb, etc.)
;
; The output installer is written to ..\output\ClashYuka-Setup-0.1.0.exe

#define MyAppName          "ClashYuka"
#define MyAppVersion       "0.1.0"
#define MyAppPublisher     "ClashYuka"
#define MyAppURL           "https://github.com/clashyuka/clashyuka"
#define MyAppExeName       "ClashYuka.exe"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
AppId={{8F2A4E7B-3C1D-4E5F-9A6B-7C8D9E0F1A2B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\output
OutputBaseFilename=ClashYuka-Setup-0.1.0
SetupIconFile=..\resources\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; Show install dialog in user's preferred language
ShowLanguageDialog=yes
LanguageDetectionMethod=uilanguage
CloseApplications=force
RestartIfNeededByRun=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "Launch ClashYuka on system startup"; GroupDescription: "Additional options:"
Name: "cleanconfig"; Description: "Also remove user configuration and profiles on uninstall"; GroupDescription: "Uninstall options:"

[Files]
; Main executable
Source: "..\build\Release\ClashYuka.exe"; DestDir: "{app}"; Flags: ignoreversion
; Resource directory: mihomo cores, GeoIP databases, service helper
Source: "..\resources\mihomo.exe"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\mihomo.exe'))
Source: "..\resources\mihomo-alpha.exe"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\mihomo-alpha.exe'))
Source: "..\resources\clash-rs.exe"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\clash-rs.exe'))
Source: "..\resources\clash-rs-alpha.exe"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\clash-rs-alpha.exe'))
Source: "..\resources\clash-yuka-service.exe"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\clash-yuka-service.exe'))
Source: "..\resources\Country.mmdb"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\Country.mmdb'))
Source: "..\resources\geoip.dat"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\geoip.dat'))
Source: "..\resources\geosite.dat"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\geosite.dat'))
Source: "..\resources\geoip.metadb"; DestDir: "{app}\resources"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{src}\..\resources\geoip.metadb'))
; Catch-all: copy any remaining resources/ files (subfolders preserved)
Source: "..\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "mihomo.exe,mihomo-alpha.exe,clash-rs.exe,clash-rs-alpha.exe,clash-yuka-service.exe,Country.mmdb,geoip.dat,geosite.dat,geoip.metadb,icon.ico"
; wxWidgets / VC++ runtime DLLs if shipped alongside the exe (optional)
Source: "..\build\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion; Check: DirExists(ExpandConstant('{src}\..\build\Release'))

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Add/Remove Programs extra metadata
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppName}"; ValueType: string; ValueName: "DisplayVersion"; ValueData: "{#MyAppVersion}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppName}"; ValueType: string; ValueName: "Publisher"; ValueData: "{#MyAppPublisher}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppName}"; ValueType: string; ValueName: "URLInfoAbout"; ValueData: "{#MyAppURL}"; Flags: uninsdeletekey
; Optional autostart entry (HKCU Run key, only when startup task selected)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startup
; Install path registration (for self-update lookups)
Root: HKLM; Subkey: "Software\{#MyAppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey

[Run]
; Optional launch after install
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent runascurrentuser

[UninstallRun]
; Quit a running instance before uninstalling
Filename: "{cmd}"; Parameters: "/C taskkill /IM {#MyAppExeName} /F"; RunOnceId: "KillApp"; Flags: runhidden

[UninstallDelete]
; Remove the resources folder and logs/profiles cache created at runtime
Type: filesandordirs; Name: "{app}\resources"
Type: dirifempty; Name: "{app}"
; Conditionally remove user configuration when the cleanconfig task was selected
Type: filesandordirs; Name: "{%APPDATA}\{#MyAppName}"; Tasks: cleanconfig
Type: filesandordirs; Name: "{%LOCALAPPDATA}\{#MyAppName}"; Tasks: cleanconfig

[Code]
// ---- Optional custom install logic ----
// Note: FileExists / DirExists are Inno Setup built-in functions and are used
// directly by the [Files] Check directives above; do not redeclare them here.

// Detect a previously installed version and offer to migrate / clean it.
function InitializeSetup(): Boolean;
var
  PrevPath: string;
begin
  Result := True;
  // Query the registry for an existing install path (upgrades only).
  if RegQueryStringValue(HKLM, 'Software\{#MyAppName}', 'InstallPath', PrevPath) then
  begin
    // Inno Setup will handle the upgrade automatically via AppId matching;
    // here we just allow the install to proceed.
    Log('Previous install detected at: ' + PrevPath);
  end;
end;

// Perform post-install migration (e.g. preserve user config across upgrades).
function CurStepChanged(CurStep: TSetupStep): Boolean;
begin
  Result := True;
  if CurStep = ssPostInstall then
  begin
    // Configuration lives under %APPDATA%\ClashYuka and is preserved across
    // upgrades by default (not part of the install directory).
    Log('ClashYuka install completed.');
  end;
end;
