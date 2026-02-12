; ModuleGit Installer Script for Inno Setup
; Builds a Windows installer with wizard, PATH integration, and uninstaller

[Setup]
AppName=ModuleGit
AppVersion=1.0.0
AppVerName=ModuleGit 1.0.0
AppPublisher=Enmanuel Reynoso
AppPublisherURL=https://github.com/EnmanuelReynoso23/Modulegit
AppSupportURL=https://github.com/EnmanuelReynoso23/Modulegit/issues
DefaultDirName={autopf}\ModuleGit
DefaultGroupName=ModuleGit
OutputDir=..\output
OutputBaseFilename=ModuleGit-Setup
Compression=lzma2
SolidCompression=yes
ChangesEnvironment=yes
UninstallDisplayName=ModuleGit
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to ModuleGit
WelcomeLabel2=This will install ModuleGit on your computer.%n%nModuleGit adds modular development to Git. Define modules in a .modgit file and work on only the files you need.%n%n13 commands. Zero dependencies. Built into Git.

[Types]
Name: "full"; Description: "Full installation (recommended)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "main"; Description: "ModuleGit binary and libraries"; Types: full custom; Flags: fixed
Name: "path"; Description: "Add ModuleGit to system PATH"; Types: full

[Files]
Source: "..\dist\git-modgit.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: main
Source: "..\dist\*.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: main

[Icons]
Name: "{group}\ModuleGit Documentation"; Filename: "https://modulegit.vercel.app"
Name: "{group}\Uninstall ModuleGit"; Filename: "{uninstallexe}"

[Registry]
; Add {app} to system PATH
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; \
    Components: path; Check: NeedsAddPath('{app}')

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath)
  then begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path: string;
  AppDir: string;
  P: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    // Remove from PATH on uninstall
    if RegQueryStringValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'Path', Path) then
    begin
      AppDir := ExpandConstant('{app}');
      P := Pos(';' + AppDir, Path);
      if P > 0 then
      begin
        Delete(Path, P, Length(';' + AppDir));
        RegWriteExpandStringValue(HKEY_LOCAL_MACHINE,
          'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
          'Path', Path);
      end;
    end;
  end;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Run]
Filename: "cmd.exe"; Parameters: "/k echo ModuleGit installed successfully! && echo. && echo You can now use 'git-modgit modgit list' in any terminal. && echo Open a NEW terminal window for PATH changes to take effect. && echo. && echo Visit https://modulegit.vercel.app for documentation. && echo. && pause"; \
    Description: "Show installation summary"; Flags: postinstall nowait skipifsilent
