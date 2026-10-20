; Stinger installer (Inno Setup 6). Built by release.ps1:
;   ISCC.exe /DAppVersion=0.2.0-alpha /DStage=<dist folder> installer\Stinger.iss
; Installs for the current user (no admin): %LOCALAPPDATA%\Programs\Stinger, with the download tools in tools\
; (yt-dlp can update itself there). Settings in %APPDATA%\Stinger and show files are never touched.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "..\dist"
#endif
#ifndef Exe
  #define Exe "Stinger.exe"
#endif

[Setup]
AppId={{6E3C2B71-5D0A-4E8B-9C47-3B1F0A9D2E55}
AppName=Stinger
AppVersion={#AppVersion}
AppVerName=Stinger {#AppVersion}
AppPublisher=LEFTNoiseAudio
DefaultDirName={localappdata}\Programs\Stinger
DefaultGroupName=Stinger
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#Stage}
OutputBaseFilename=Stinger-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\build\Stinger_artefacts\JuceLibraryCode\icon.ico
UninstallDisplayIcon={app}\Stinger.exe
UninstallDisplayName=Stinger
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "streamdeck"; Description: "Install the Stream Deck plugin (needs the Stream Deck app)"; GroupDescription: "Stream Deck:"; Flags: unchecked

[Files]
Source: "{#Stage}\{#Exe}"; DestDir: "{app}"; DestName: "Stinger.exe"; Flags: ignoreversion
Source: "{#Stage}\Stinger.streamDeckPlugin"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\tools\bin\yt-dlp.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\tools\bin\ffmpeg.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\tools\bin\deno.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\tools\bin\ffmpeg-LICENSE.txt"; DestDir: "{app}\tools"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\Stinger"; Filename: "{app}\Stinger.exe"
Name: "{autodesktop}\Stinger"; Filename: "{app}\Stinger.exe"; Tasks: desktopicon

[Registry]
; double-clicking a .stinger show opens it in Stinger
Root: HKA; Subkey: "Software\Classes\.stinger"; ValueType: string; ValueName: ""; ValueData: "Stinger.Show"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Stinger.Show"; ValueType: string; ValueName: ""; ValueData: "Stinger show"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Stinger.Show\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Stinger.exe,0"
Root: HKA; Subkey: "Software\Classes\Stinger.Show\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Stinger.exe"" ""%1"""

[Run]
Filename: "{app}\Stinger.streamDeckPlugin"; Description: "Install the Stream Deck plugin"; Flags: shellexec postinstall skipifsilent; Tasks: streamdeck
Filename: "{app}\Stinger.exe"; Description: "Start Stinger"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; yt-dlp -U leaves its old copy behind
Type: files; Name: "{app}\tools\*.old"
