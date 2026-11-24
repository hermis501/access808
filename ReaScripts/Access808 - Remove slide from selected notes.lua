-- Access808: stop each selected note from sliding into the next note.
--
-- Shortens every selected note that overlaps the next note so it ends just
-- before the next note starts. The next note is then a fresh hit with punch
-- and click instead of a slide.
--
-- Works in the MIDI editor, or on the selected item in the arrange view.
-- Results are spoken through OSARA.

local scriptPath = debug.getinfo(1, "S").source:match("^@(.*[\\/])")
local A = dofile(scriptPath .. "Access808_common.lua")

local take = A.getTake()
if not take then
  A.say("No MIDI take. Open a MIDI item in the MIDI editor, or select a MIDI item.", true)
  return
end

local notes = A.readNotes(take)
local removed, selectedCount = 0, 0

reaper.Undo_BeginBlock()
for i, n in ipairs(notes) do
  if n.selected then
    selectedCount = selectedCount + 1
    local nextNote = A.nextNote(notes, i)
    if nextNote and n.endPpq > nextNote.startPpq then
      -- One tick before the next note guarantees the note-off comes first.
      local newEnd = math.max(n.startPpq + 1, nextNote.startPpq - 1)
      A.setEnd(take, n, newEnd)
      removed = removed + 1
    end
  end
end
reaper.MIDI_Sort(take)
reaper.Undo_EndBlock("Access808: remove slide from selected notes", -1)

if selectedCount == 0 then
  A.say("No notes selected.", true)
  return
end

if removed == 0 then
  A.say("None of the selected notes slide.")
else
  A.say("Slide removed from " .. A.plural(removed, "note") .. ".")
end
