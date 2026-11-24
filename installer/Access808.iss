; Access808 installer (Inno Setup 6). Build with:
;   cmake --build build --config Release --target installer
; or directly:
;   ISCC /DAppVersion=0.1.0 /DVst3Dir=<path to Access808.vst3> /DOutputDir=<dir> installer\Access808.iss
;
; Installs the VST3 to the system VST3 folder and, optionally, the REAPER
; slide scripts. The scripts are also registered in REAPER's Actions list
; (main and MIDI editor sections) by adding lines to reaper-kb.ini, which
; needs REAPER to be closed.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Vst3Dir
  #define Vst3Dir "..\build\Access808_artefacts\Release\VST3\Access808.vst3"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\installer"
#endif

[Setup]
AppId={{04DC509A-A830-4F36-8319-3D2FF88747A9}
AppName=Access808
AppVersion={#AppVersion}
AppVerName=Access808 {#AppVersion}
AppPublisher=Hermis Kasperavičius
DefaultDirName={commonpf64}\Access808
DefaultGroupName=Access808
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Access808-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UsedUserAreasWarning=no
CloseApplications=yes
UninstallDisplayName=Access808 {#AppVersion}
InfoAfterFile=AfterInstall.txt

[Tasks]
Name: "reaperscripts"; Description: "Install the REAPER slide scripts and add them to the REAPER Actions list"; GroupDescription: "REAPER:"

[Files]
Source: "{#Vst3Dir}\*"; DestDir: "{commoncf64}\VST3\Access808.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\ReaScripts\*.lua"; DestDir: "{code:GetScriptsDir}"; Tasks: reaperscripts; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "Access808 manual.txt"; Flags: ignoreversion
Source: "AfterInstall.txt"; DestDir: "{app}"; DestName: "Quick start.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\Access808 manual"; Filename: "{app}\Access808 manual.txt"
Name: "{group}\Access808 quick start"; Filename: "{app}\Quick start.txt"
Name: "{group}\Uninstall Access808"; Filename: "{uninstallexe}"

[Registry]
Root: HKA; Subkey: "Software\Access808"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Access808"; ValueType: string; ValueName: "ReaperResourcePath"; ValueData: "{code:GetReaperDir}"; Tasks: reaperscripts; Flags: uninsdeletevalue

[Code]
const
  RegKey = 'Software\Access808';

var
  ReaperPage: TInputDirWizardPage;
  SkipActions: Boolean;

#include "ReaperActions.iss"

{ ---- Folders ---- }

function GetReaperDir(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(ReaperPage.Values[0]);
end;

function GetScriptsDir(Param: String): String;
begin
  Result := GetReaperDir('') + '\Scripts\Access808';
end;

function IsReaperRunning: Boolean;
begin
  Result := FindWindowByClassName('REAPERwnd') <> 0;
end;

{ ---- Wizard ---- }

procedure InitializeWizard;
begin
  ReaperPage := CreateInputDirPage(wpSelectTasks,
    'REAPER resource folder',
    'Where is your REAPER resource folder?',
    'The slide scripts go into the Scripts folder inside it. For a standard REAPER installation, keep the folder below. ' +
    'For a portable REAPER, choose its folder. In REAPER, Options, Show REAPER resource path shows the right folder.',
    False, '');
  ReaperPage.Add('REAPER resource folder:');
  ReaperPage.Values[0] := GetPreviousData('ReaperResourcePath', ExpandConstant('{userappdata}\REAPER'));
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'ReaperResourcePath', ReaperPage.Values[0]);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = ReaperPage.ID) and not WizardIsTaskSelected('reaperscripts');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = ReaperPage.ID then
  begin
    if not FileExists(AddBackslash(ReaperPage.Values[0]) + 'reaper.ini') then
      Result := MsgBox('There is no reaper.ini in this folder, so it may not be a REAPER resource folder. Install the scripts here anyway?',
                       mbConfirmation, MB_YESNO) = IDYES;
  end;

  if (CurPageID = wpReady) and WizardIsTaskSelected('reaperscripts') then
  begin
    SkipActions := False;
    while IsReaperRunning and not SkipActions do
      if MsgBox('REAPER is running. Close REAPER, then choose Retry, so the slide actions can be added to the Actions list. ' +
                'Choose Cancel to copy the scripts without adding the actions.', mbError, MB_RETRYCANCEL) = IDCANCEL then
        SkipActions := True;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('reaperscripts') then
  begin
    if SkipActions then
      MsgBox('The scripts were copied to ' + GetScriptsDir('') + '. To add them, open REAPER''s Actions list and use New action, Load ReaScript.',
             mbInformation, MB_OK)
    else
      AddOurActions(GetReaperDir('') + '\reaper-kb.ini');
  end;
end;

{ ---- Uninstall ---- }

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ReaperDir: String;
begin
  if not RegQueryStringValue(HKA, RegKey, 'ReaperResourcePath', ReaperDir) then
    Exit;

  if CurUninstallStep = usUninstall then
  begin
    while IsReaperRunning do
      if MsgBox('REAPER is running. Close REAPER, then choose Retry, so the slide actions can be removed from the Actions list. ' +
                'Choose Cancel to leave the actions in place.', mbError, MB_RETRYCANCEL) = IDCANCEL then
        Exit;
    RemoveOurActions(AddBackslash(ReaperDir) + 'reaper-kb.ini');
  end;

  if CurUninstallStep = usPostUninstall then
    RemoveDir(AddBackslash(ReaperDir) + 'Scripts\Access808');
end;
