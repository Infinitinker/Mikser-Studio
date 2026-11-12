; The Mikser Studio installer, compiled by tools/build_release.py with Inno Setup 7 (ISCC.exe) from the
; same folder as the Windows zip. It installs for the current user, with no admin rights, into
; %LOCALAPPDATA%\Programs\Mikser Studio, or, if the user chooses and is an administrator, for all users
; into Program Files. Either way each user's settings and cache stay in their own
; %LOCALAPPDATA%\Mikser Studio.
;
; build_release.py passes:
;   AppVersion      the release, e.g. 0.1.0
;   NumericVersion  its four numbers for the Windows version resource, e.g. 0.1.0.0
;   SourceDir       the folder the release was assembled in, as in the Windows zip
;   OutputDir       where the installer is written

#if !Defined(AppVersion) || !Defined(NumericVersion) || !Defined(SourceDir) || !Defined(OutputDir)
  #error Build the installer with tools/build_release.py, which passes the values this script needs.
#endif

#define AppName "Mikser Studio"
#define FileName "MikserStudio"
#define AppExe "bin\mikser-studio.exe"
#define Repository "https://github.com/Infinitinker/Mikser-Studio"
#define AppGuid "263555C3-8F27-4582-ADAD-3B3E37B7E95A"

[Setup]
; Never change AppId: it is how an update finds the installed Mikser Studio.
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Infinitinker
AppPublisherURL={#Repository}
AppSupportURL={#Repository}/issues
AppUpdatesURL={#Repository}/releases
VersionInfoVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
PrivilegesRequired=lowest
; Setup asks "for me only" or "for all users" (or takes /CURRENTUSER or /ALLUSERS), offering the
; previous choice on an update; {autopf}, {autoprograms} and {autodesktop} follow it.
PrivilegesRequiredOverridesAllowed=dialog commandline
SetupArchitecture=x64
MinVersion=10.0
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#FileName}-{#AppVersion}-windows-x64-setup
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
; Setup and Uninstall offer to close a running Mikser Studio, whose files they replace or remove; a
; silent install closes it without asking. Mikser Studio is started again from the last page, not by
; Windows.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.DeleteUserData=Also delete your Mikser Studio settings, workspaces and the cache with the AI's results?%n%n%1
english.UserDataNotDeleted=Some files in %1 could not be deleted. Delete the folder yourself.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; The libraries and MLT's and Qt's files change between releases; old ones must not stay mixed in
; with the new.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\lib"
Type: filesandordirs; Name: "{app}\share"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; As the user who ran Setup, not as the administrator an all-users install runs as: Mikser Studio
; keeps that user's settings.
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
// The uninstaller removes the program folder; the user's own data is removed only if they say so, and
// only for an install of their own: an all-users uninstall runs as an administrator, whose folder may
// not be the one of the user who asked, and leaves each user's data alone.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  UserData: String;
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent and not IsAdminInstallMode then
  begin
    UserData := ExpandConstant('{localappdata}\{#AppName}');
    if DirExists(UserData) and
       (MsgBox(FmtMessage(CustomMessage('DeleteUserData'), [UserData]), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) and
       not DelTree(UserData, True, True, True) then
      MsgBox(FmtMessage(CustomMessage('UserDataNotDeleted'), [UserData]), mbError, MB_OK);
  end;
end;
