#ifndef PayloadDir
  #error PayloadDir must point to the deployed preview
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef AppVersion
  #define AppVersion "6.1.0"
#endif

[Setup]
AppId={{C2FD0142-DBA5-4D98-8EAC-82786E4671A1}
AppName=Desk
AppVersion={#AppVersion}
AppPublisher=coderleexl
AppPublisherURL=https://github.com/coderleexl/moonlight-qt
DefaultDirName={localappdata}\Programs\Desk
DefaultGroupName=Desk
UsePreviousGroup=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=Desk-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Desk.exe
SetupIconFile=..\..\app\moonlight.ico
CloseApplicationsFilter=Desk.exe,MoonlightDeskPreview.exe
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\MoonlightDeskPreview.exe"
Type: files; Name: "{userdesktop}\Moonlight Desk Preview.lnk"
Type: files; Name: "{userprograms}\Moonlight Desk Preview\Moonlight Desk Preview.lnk"
Type: dirifempty; Name: "{userprograms}\Moonlight Desk Preview"

[Icons]
Name: "{group}\Desk"; Filename: "{app}\Desk.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Desk"; Filename: "{app}\Desk.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Desk.exe"; Description: "Launch Desk"; Flags: nowait postinstall skipifsilent
