[Setup]
AppId=OLED.DesktopMode
AppName=OLED Desktop Mode
AppVersion=1.3.0
DefaultDirName={localappdata}\Programs\OLED Desktop Mode
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\OledDesktopMode.exe
SetupIconFile=..\assets\oled.ico
OutputDir=..\build\installer
OutputBaseFilename=OLED-Desktop-Mode-Setup
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=yes

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[CustomMessages]
pt.Startup=Iniciar o modo OLED ao entrar no Windows
en.Startup=Start OLED Desktop Mode when signing in to Windows
es.Startup=Iniciar OLED Desktop Mode al iniciar sesión en Windows
pt.OpenApp=Abrir OLED Desktop Mode
en.OpenApp=Open OLED Desktop Mode
es.OpenApp=Abrir OLED Desktop Mode

[Tasks]
Name: "startup"; Description: "{cm:Startup}"

[Files]
Source: "..\build\OledDesktopMode.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs; Excludes: "ui-preview*.png,ui-verification.txt,*.pdb"

[Icons]
Name: "{userprograms}\OLED Desktop Mode"; Filename: "{app}\OledDesktopMode.exe"
Name: "{userstartup}\OLED Desktop Mode"; Filename: "{app}\OledDesktopMode.exe"; Parameters: "--background"; Tasks: startup

[InstallDelete]
Type: files; Name: "{userstartup}\OLED Desktop Mode.lnk"; Tasks: not startup

[Run]
Filename: "{app}\OledDesktopMode.exe"; Description: "{cm:OpenApp}"; Flags: nowait postinstall skipifsilent
