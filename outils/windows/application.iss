; GrymoiR : l'installateur d'une application fabriquée (docs/atelier.md, § 8), pour le client final.
; Compilé par outils/fabriquer-ci (Windows), qui passe l'identité du projet (projet.grymatelier) :
;   /DNom=<nom affiché> /DVersion=<1.0> /DEditeur=<éditeur, ou vide> /DIdent=<identifiant> /DIcone=<fichier .ico>
;   /DSource=<dossier préparé, avec Nom.exe> /DSortie=<dossier de l'installateur>
; Installation pour l'utilisateur seul, sans droits d'administrateur, dans %LOCALAPPDATA%\Programs\<Nom>.
; La désinstallation retire l'application, jamais ses données (le dossier que Windows lui donne), qui survivent ainsi
; à une réinstallation ou à une mise à jour.

#ifndef Version
  #define Version "1.0"
#endif
#ifndef Editeur
  #define Editeur ""
#endif

[Setup]
; un même identifiant d'une version à l'autre : une nouvelle version remplace l'ancienne au lieu de s'y ajouter
AppId=GrymoiR.application.{#Ident}
AppName={#Nom}
AppVersion={#Version}
AppVerName={#Nom} {#Version}
#if Editeur != ""
AppPublisher={#Editeur}
#endif
DefaultDirName={localappdata}\Programs\{#Nom}
DefaultGroupName={#Nom}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#Sortie}
OutputBaseFilename={#Nom}-installation
SetupIconFile={#Icone}
UninstallDisplayIcon={app}\{#Nom}.exe
UninstallDisplayName={#Nom}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "bureau"; Description: "Mettre {#Nom} sur le bureau"; Flags: unchecked

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#Nom}"; Filename: "{app}\{#Nom}.exe"
Name: "{autodesktop}\{#Nom}"; Filename: "{app}\{#Nom}.exe"; Tasks: bureau

[Run]
Filename: "{app}\{#Nom}.exe"; Description: "Ouvrir {#Nom}"; Flags: nowait postinstall skipifsilent
