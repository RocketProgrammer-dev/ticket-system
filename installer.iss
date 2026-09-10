; Inno Setup script for the Ticket System client.
;
; Build with: right-click this file > Compile, or
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
;
; Inno Setup is free and has no licensing restrictions on the software it
; packages. Qt's own Installer Framework is GPL-licensed, which would force
; this application to be GPL too — see the licensing note in the README.

#define AppName "Ticket System"
#define AppVersion "1.0.0"
#define AppPublisher "Ticket System"
#define AppExeName "TicketApp.exe"

; Point this at the folder windeployqt prepared. Everything in it is copied
; into the installation directory.
#define SourceDir "deploy\windows"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\TicketSystem
DefaultGroupName={#AppName}
OutputDir=installer-output
OutputBaseFilename=TicketSystemSetup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

; Icon shown on the setup executable itself and in Add/Remove Programs.
SetupIconFile=resources\appicon.ico
UninstallDisplayIcon={app}\{#AppExeName}

; Installing under Program Files needs administrator rights. That is
; deliberate: it is also what makes ticketapp.ini unwritable by ordinary
; users, so nobody can repoint the client at another server.
PrivilegesRequired=admin

; Refuse to install on 32-bit Windows rather than failing at first launch.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"

[CustomMessages]
; Inno Setup translates its own pages via the .isl files above, but a custom
; page created in [Code] is not covered by them — its text has to be supplied
; here, once per language, and looked up with {cm:...} at runtime.
english.ServerPageCaption=Server address
english.ServerPageDescription=Where is the Ticket System API running?
english.ServerPageSubCaption=Enter the address of the server. Use the server's name or IP address, not localhost, unless the API runs on this same machine.
english.ServerPagePrompt=API address:
english.ServerPageInvalid=The address must start with http:// or https://

turkish.ServerPageCaption=Sunucu adresi
turkish.ServerPageDescription=Ticket System API'si nerede çalışıyor?
turkish.ServerPageSubCaption=Sunucunun adresini girin. API bu bilgisayarda çalışmıyorsa localhost yerine sunucunun adını veya IP adresini kullanın.
turkish.ServerPagePrompt=API adresi:
turkish.ServerPageInvalid=Adres http:// veya https:// ile başlamalıdır

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The executable and every Qt DLL and plugin windeployqt collected.
; recursesubdirs matters: the plugins live in subfolders (platforms\,
; styles\, imageformats\) and the app will not start without them.
Source: "{#SourceDir}\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; The deployment config is installed only if absent, so reinstalling or
; upgrading does not overwrite the address an administrator already set.
Source: "ticketapp.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; \
    Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; \
    Flags: nowait postinstall skipifsilent

[Code]
// Asks for the API address during installation and writes it into
// ticketapp.ini. Without this the installer would ship a hardcoded address
// and every deployment would need the file edited by hand afterwards.

var
  ServerPage: TInputQueryWizardPage;

procedure InitializeWizard;
begin
  ServerPage := CreateInputQueryPage(wpSelectDir,
    ExpandConstant('{cm:ServerPageCaption}'),
    ExpandConstant('{cm:ServerPageDescription}'),
    ExpandConstant('{cm:ServerPageSubCaption}'));

  ServerPage.Add(ExpandConstant('{cm:ServerPagePrompt}'), False);
  ServerPage.Values[0] := 'http://ticket-server:5000';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Url: String;
begin
  Result := True;

  if CurPageID = ServerPage.ID then
  begin
    Url := Trim(ServerPage.Values[0]);

    // A minimal sanity check. Catching "ticket-server:5000" without a scheme
    // here is much kinder than a confusing connection error on first launch.
    if (Pos('http://', Url) <> 1) and (Pos('https://', Url) <> 1) then
    begin
      MsgBox(ExpandConstant('{cm:ServerPageInvalid}'), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  IniPath: String;
begin
  if CurStep = ssPostInstall then
  begin
    IniPath := ExpandConstant('{app}\ticketapp.ini');
    SetIniString('Server', 'Url', Trim(ServerPage.Values[0]), IniPath);
  end;
end;

[Registry]
Root: HKCU; Subkey: "Software\TicketSystem"; Flags: uninsdeletekey