; NightShift's setup program (Inno Setup 6.7+). Don't compile this by hand: setup.cmd (installer\build_setup.ps1)
; passes the version, the built exe, its icon, the wizard images and the NightShift Lofi effects, then
; writes build\installer\NightShift-Setup-<version>.exe.
;
; Installs for the user running it (no admin prompt), where install.cmd does:
; %LOCALAPPDATA%\Programs\Nightshift\Nightshift.exe. Keep that path: Windows Security's Controlled folder
; access allows NightShift by it. Start menu and desktop shortcuts, .nsproj projects opening in NightShift
; (with its icon), the Lofi effects in the per-user VST3 folder every plugin host looks in, and an entry
; in Settings > Apps to uninstall it. Uninstalling leaves settings, drum kits, instruments and projects.
; NightShift is free software under the GNU GPL v3 or later; the setup puts the licence's text beside it.
; (No licence page to accept: the GPL asks nothing of someone who only runs the program.)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define AppName "NightShift"
#define AppExeName "Nightshift.exe"

[Setup]
; The AppId must never change: it's how a new setup finds the copy already installed
AppId={{26B3AD13-FFBA-4B2B-A0FC-FAD61D5BA3A5}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=NightWriter
AppCopyright=Copyright (C) 2026 NightWriter. Free software under the GNU GPL v3 or later.
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} setup
PrivilegesRequired=lowest
DefaultDirName={autopf}\Nightshift
DisableProgramGroupPage=yes
DisableReadyPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
WizardStyle=modern dark
WizardImageFile={#Images}\wizard_100.bmp,{#Images}\wizard_150.bmp,{#Images}\wizard_200.bmp
WizardSmallImageFile={#Images}\small_100.bmp,{#Images}\small_150.bmp,{#Images}\small_200.bmp
WizardImageAlphaFormat=defined
OutputBaseFilename=NightShift-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes

[Tasks]
Name: desktopicon; Description: "Put NightShift on the desktop"
Name: associate; Description: "Open .nsproj projects in NightShift (double-click one in Explorer)"
#ifdef Lofi
Name: lofi; Description: "Install the NightShift Lofi effects (Cassette, Vinyl, Crush, Haze) for NightShift and your other plugin hosts"
#endif
#ifdef Bass
Name: bass; Description: "Install the NightShift Bass effects (Drive, Split) for NightShift and your other plugin hosts"
#endif
#ifdef Drums
Name: drums; Description: "Install Nightshift Drums (the studio drum plugin, and its own app in the Start menu)"
#endif

[InstallDelete]
; What install.cmd leaves beside the exe: a copy renamed aside while it ran, and its shortcut icon
Type: files; Name: "{app}\Nightshift.old*.exe"
Type: files; Name: "{app}\NightShift-*.ico"

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
; NightShift is GPLv3-or-later: everyone who gets it gets the licence's text too (About's Licence... opens it)
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
; Who made the libraries and sounds NightShift uses, and their licences
Source: "..\NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
#ifdef Bass
Source: "{#Bass}\NightshiftBassDrive_artefacts\Release\VST3\Nightshift Bass Drive.vst3\*"; DestDir: "{localappdata}\Programs\Common\VST3\Nightshift Bass Drive.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: bass
Source: "{#Bass}\NightshiftBassSplit_artefacts\Release\VST3\Nightshift Bass Split.vst3\*"; DestDir: "{localappdata}\Programs\Common\VST3\Nightshift Bass Split.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: bass
#endif
#ifdef Drums
; Where nightshift-drums' install.cmd puts them (its kits go in a Kits folder beside the app, which uninstalling leaves)
Source: "{#Drums}\NightshiftDrums_artefacts\Release\VST3\Nightshift Drums.vst3\*"; DestDir: "{localappdata}\Programs\Common\VST3\Nightshift Drums.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: drums
Source: "{#Drums}\NightshiftDrums_artefacts\Release\Standalone\Nightshift Drums.exe"; DestDir: "{localappdata}\Programs\Nightshift Drums"; Flags: ignoreversion; Tasks: drums
#endif
#ifdef Lofi
Source: "{#Lofi}\NightshiftCassette_artefacts\Release\VST3\Nightshift Cassette.vst3\*"; DestDir: "{localappdata}\Programs\Common\VST3\Nightshift Cassette.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: lofi
Source: "{#Lofi}\NightshiftVinyl_artefacts\Release\VST3\Nightshift Vinyl.vst3\*"; DestDir: "{localappdata}\Programs\Common\VST3\Nightshift Vinyl.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: lofi
Source: "{#Lofi}\NightshiftCrush_artefacts\Release\VST3\Nightshift Crush.vst3\*"; DestDir: "{localappdata}\Programs\Common\VST3\Nightshift Crush.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: lofi
Source: "{#Lofi}\NightshiftHaze_artefacts\Release\VST3\Nightshift Haze.vst3\*"; DestDir: "{localappdata}\Programs\Common\VST3\Nightshift Haze.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: lofi
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon
#ifdef Drums
Name: "{autoprograms}\Nightshift Drums"; Filename: "{localappdata}\Programs\Nightshift Drums\Nightshift Drums.exe"; WorkingDir: "{localappdata}\Programs\Nightshift Drums"; Tasks: drums
#endif

[Registry]
; .nsproj -> NightShift.Project, opened by "Nightshift.exe <file>" (a running NightShift is handed the file)
; (the .nsproj keys go too on uninstall, if nothing else put anything in them)
Root: HKA; Subkey: "Software\Classes\.nsproj"; ValueType: string; ValueName: ""; ValueData: "NightShift.Project"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.nsproj\OpenWithProgids"; ValueType: string; ValueName: "NightShift.Project"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\NightShift.Project"; ValueType: string; ValueName: ""; ValueData: "NightShift project"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\NightShift.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\NightShift.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Open NightShift now"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; (LICENSE.txt is the setup's own file, so it goes anyway; install.cmd's copy of it too)
Type: files; Name: "{app}\LICENSE.txt"
Type: files; Name: "{app}\Nightshift.old*.exe"
Type: files; Name: "{app}\NightShift-*.ico"
