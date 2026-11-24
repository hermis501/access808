{ REAPER action registration for the Access808 slide scripts.
  Included by Access808.iss and tests\ReaperActionsTest.iss. }

const
  MidiEditorSection = 32060;

{ ---- Scripts and their REAPER action IDs (stable, so reinstalling never duplicates) ---- }

function ScriptCount: Integer;
begin
  Result := 3;
end;

function ScriptName(I: Integer): String;
begin
  case I of
    0: Result := 'Access808 - Slide selected notes into next note.lua';
    1: Result := 'Access808 - Remove slide from selected notes.lua';
  else
    Result := 'Access808 - Set slide overlap.lua';
  end;
end;

function ScriptHash(I: Integer): String;
begin
  case I of
    0: Result := '0df1e9742e0b8bf5226acd8918a2ceb101490308';
    1: Result := 'ed2400d7cd7693647e80530614c1eca4f5010f8f';
  else
    Result := '2ac7567399c30fe231c53f5e137f730395290b3f';
  end;
end;

function ActionId(I, Section: Integer): String;
begin
  if Section = MidiEditorSection then
    Result := 'RS7d3c_' + ScriptHash(I)
  else
    Result := 'RS' + ScriptHash(I);
end;

function ActionLine(I, Section: Integer): String;
begin
  Result := 'SCR 4 ' + IntToStr(Section) + ' ' + ActionId(I, Section) +
            ' "Custom: ' + ScriptName(I) + '" "Access808/' + ScriptName(I) + '"';
end;

{ ---- reaper-kb.ini editing ---- }

function LineIsOurs(const Line: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to ScriptCount - 1 do
    if (Pos(' ' + ActionId(I, 0) + ' ', Line) > 0) or (Pos(' ' + ActionId(I, MidiEditorSection) + ' ', Line) > 0) then
    begin
      Result := True;
      Exit;
    end;
end;

{ reaper-kb.ini is edited as raw bytes (AnsiString), so the user's own lines,
  which may be UTF-8 in any language, are kept byte for byte. Our lines are ASCII. }

procedure RemoveOurActions(const KbFile: String);
var
  Raw, Rest, Line, Kept: AnsiString;
  P: Integer;
  Changed: Boolean;
begin
  if not LoadStringFromFile(KbFile, Raw) then
    Exit;
  Rest := Raw;
  Kept := '';
  Changed := False;
  while Length(Rest) > 0 do
  begin
    P := Pos(#10, Rest);
    if P = 0 then
    begin
      Line := Rest;
      Rest := '';
    end
    else
    begin
      Line := Copy(Rest, 1, P);
      Rest := Copy(Rest, P + 1, Length(Rest) - P);
    end;
    if LineIsOurs(String(Line)) then
      Changed := True
    else
      Kept := Kept + Line;
  end;
  if Changed then
    SaveStringToFile(KbFile, Kept, False);
end;

procedure AddOurActions(const KbFile: String);
var
  Raw, Added: AnsiString;
  I: Integer;
begin
  { Remove old copies first so the lines always match this version. }
  RemoveOurActions(KbFile);
  Added := '';
  if LoadStringFromFile(KbFile, Raw) and (Length(Raw) > 0) and (Raw[Length(Raw)] <> #10) then
    Added := #13#10;
  for I := 0 to ScriptCount - 1 do
    Added := Added + AnsiString(ActionLine(I, 0)) + #13#10 + AnsiString(ActionLine(I, MidiEditorSection)) + #13#10;
  if not SaveStringToFile(KbFile, Added, True) then
    MsgBox('Could not add the actions to ' + KbFile + '. Load the scripts in REAPER''s Actions list with New action, Load ReaScript.', mbError, MB_OK);
end;

