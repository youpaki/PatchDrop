; PatchLink installer - Inno Setup 6
; Builds PatchLink-Setup-1.0.0.exe (no zip needed, everything automated)
; Compile: ISCC.exe installer.iss   (run from patchlink/installer/)

#define MyAppName "PatchLink"
#define MyAppVersion "1.4.4"
#define MyAppPublisher "leve"
#define MyAppURL "https://gg.trenas.fr/patchdrop/"

[Setup]
AppId={{8F3A2B1C-7D4E-4A6F-9C0B-PATCHLINK10}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
LicenseFile=..\..\LICENSE
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#MyAppName} {#MyAppVersion}
OutputDir=..\..\patchdrop\releases
OutputBaseFilename=PatchLink-Setup-{#MyAppVersion}
SetupIconFile=
DirExistsWarning=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation (recommended)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin  (FL Studio / Ableton)"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone app  (no DAW needed)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut (standalone)"; Components: standalone; Flags: unchecked
Name: "launchapp"; Description: "&Launch PatchLink after installing"; Flags: unchecked

[Files]
; VST3 goes to the fixed system location (VST3 spec - no choice offered)
Source: "..\build\PatchLink_artefacts\Release\VST3\PatchLink.vst3\*"; DestDir: "{commoncf64}\VST3\PatchLink.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
; Standalone goes to the app folder the user picks
Source: "..\build\PatchLink_artefacts\Release\Standalone\PatchLink.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\PatchLink (standalone)"; Filename: "{app}\PatchLink.exe"; Components: standalone
Name: "{group}\PatchDrop - get a patch code"; Filename: "{#MyAppURL}"; Components: standalone
Name: "{autodesktop}\PatchLink"; Filename: "{app}\PatchLink.exe"; Tasks: desktopicon; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\PatchLink.vst3"

[Run]
Filename: "{app}\PatchLink.exe"; Description: "Launch PatchLink"; Flags: nowait postinstall skipifsilent; Tasks: launchapp; Components: standalone

[Code]
var
  InfoPage: TOutputMsgMemoWizardPage;

procedure InitializeWizard;
begin
  InfoPage := CreateOutputMsgMemoPage(wpSelectComponents,
    'Where things go', 'No wrong answers here.',
    'VST3 plugins MUST live in the shared system folder, so the installer puts PatchLink there automatically:' + #13#10 +
    '  C:\Program Files\Common Files\VST3\PatchLink.vst3' + #13#10#13#10 +
    'Your DAW finds it on the next plugin rescan (FL: Plugin Manager / Ableton: Preferences > Plug-ins).' + #13#10#13#10 +
    'Existing installs are replaced, your saved patches and share codes are untouched.',
    '');
end;
