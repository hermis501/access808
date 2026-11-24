; Test of the reaper-kb.ini editing in ReaperActions.iss. Installs nothing:
; InitializeSetup runs the checks on the file given with /kb=..., writes a
; report to /report=..., and returns False so Setup exits.
;
;   ISCC /O<outdir> installer\tests\ReaperActionsTest.iss
;   <outdir>\ReaperActionsTest.exe /kb=<copy of reaper-kb.ini> /report=<report.txt>

[Setup]
AppName=Access808 action test
AppVersion=1
CreateAppDir=no
Uninstallable=no
PrivilegesRequired=lowest
OutputBaseFilename=ReaperActionsTest

[Code]
#include "..\ReaperActions.iss"

function CountOurLines(const S: AnsiString): Integer;
var
  Rest: AnsiString;
  P: Integer;
begin
  Result := 0;
  Rest := S;
  P := Pos('"Access808/', Rest);
  while P > 0 do
  begin
    Result := Result + 1;
    Rest := Copy(Rest, P + 1, Length(Rest) - P);
    P := Pos('"Access808/', Rest);
  end;
end;

function InitializeSetup: Boolean;
var
  Kb, Log: String;
  Orig, AfterAdd, AfterTwice, AfterRemove, Report: AnsiString;
begin
  Kb := ExpandConstant('{param:kb}');
  Log := ExpandConstant('{param:report}');
  LoadStringFromFile(Kb, Orig);

  AddOurActions(Kb);
  LoadStringFromFile(Kb, AfterAdd);
  AddOurActions(Kb);
  LoadStringFromFile(Kb, AfterTwice);
  RemoveOurActions(Kb);
  LoadStringFromFile(Kb, AfterRemove);

  Report := 'lines after add: ' + IntToStr(CountOurLines(AfterAdd)) + #13#10 +
            'lines after second add: ' + IntToStr(CountOurLines(AfterTwice)) + #13#10 +
            'lines after remove: ' + IntToStr(CountOurLines(AfterRemove)) + #13#10;
  if (AfterRemove = Orig) or (AfterRemove = Orig + #13#10) then
    Report := Report + 'original bytes preserved: yes' + #13#10
  else
    Report := Report + 'original bytes preserved: NO' + #13#10;
  if Copy(AfterAdd, 1, Length(Orig)) = Orig then
    Report := Report + 'add keeps original prefix: yes' + #13#10
  else
    Report := Report + 'add keeps original prefix: NO' + #13#10;
  Report := Report + '--- added part ---' + #13#10 + Copy(AfterAdd, Length(Orig) + 1, Length(AfterAdd));

  SaveStringToFile(Log, Report, False);
  Result := False;
end;
