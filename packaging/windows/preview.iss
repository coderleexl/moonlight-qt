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
AppName=Moonlight Desk Preview
AppVersion={#AppVersion}
AppPublisher=coderleexl
AppPublisherURL=https://github.com/coderleexl/moonlight-qt
DefaultDirName={localappdata}\Programs\MoonlightDeskPreview
DefaultGroupName=Moonlight Desk Preview
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=MoonlightDeskPreview-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\MoonlightDeskPreview.exe
SetupIconFile=..\..\app\moonlight.ico
CloseApplicationsFilter=MoonlightDeskPreview.exe
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Moonlight Desk Preview"; Filename: "{app}\MoonlightDeskPreview.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Moonlight Desk Preview"; Filename: "{app}\MoonlightDeskPreview.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\MoonlightDeskPreview.exe"; Description: "Launch Moonlight Desk Preview"; Flags: nowait postinstall skipifsilent
