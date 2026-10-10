; GrymoiR : l'installateur pour Windows (Inno Setup 6, https://jrsoftware.org/isinfo.php).
; Compilé par ./paquet-windows, qui prépare le dossier à installer et passe :
;   /DSource=<dossier préparé>  /DVersion=<version>  /DSortie=<dossier de l'installateur>
; Installation pour l'utilisateur seul, sans droits d'administrateur, dans %LOCALAPPDATA%\Programs\GrymoiR.
; La désinstallation retire le logiciel, jamais les projets de Documents\GrymoiR ni leurs données.

#ifndef Version
  #define Version "0.0"
#endif

[Setup]
AppId={{9A502964-D6DA-4C1D-A263-C86BCE58CB15}
AppName=GrymoiR
AppVersion={#Version}
AppVerName=GrymoiR {#Version}
AppPublisher=Alain Thiémard
DefaultDirName={localappdata}\Programs\GrymoiR
DefaultGroupName=GrymoiR
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#Sortie}
OutputBaseFilename=GrymoiR-installation
SetupIconFile=GrymoiR.ico
UninstallDisplayIcon={app}\bin\grym-atelier.exe
UninstallDisplayName=GrymoiR
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "bureau"; Description: "Mettre GrymoiR sur le bureau"; Flags: unchecked

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\GrymoiR"; Filename: "{app}\bin\grym-atelier.exe"
Name: "{autodesktop}\GrymoiR"; Filename: "{app}\bin\grym-atelier.exe"; Tasks: bureau

[Run]
Filename: "{app}\bin\grym-atelier.exe"; Description: "Ouvrir GrymoiR"; Flags: nowait postinstall skipifsilent
