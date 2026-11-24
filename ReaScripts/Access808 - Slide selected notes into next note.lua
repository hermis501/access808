-- Access808: make each selected note slide into the note after it.
--
-- Extends every selected note so that it overlaps the start of the next note
-- by the slide overlap (default a 1/64 note; change it with
-- "Access808 - Set slide overlap"). With Access808's Glide Mode set to
-- Legato, the pitch then slides into the next note.
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
local overlap = A.ppqPerQuarter(take) * 4 / A.getOverlapDivision()

local slid, lastNote, samePitch, selectedCount = 0, 0, 0, 0

reaper.Undo_BeginBlock()
for i, n in ipairs(notes) do
  if n.selected then
    selectedCount = selectedCount + 1
    local nextNote = A.nextNote(notes, i)
    if not nextNote then
      lastNote = lastNote + 1
    elseif nextNote.pitch == n.pitch then
      -- Overlapping two notes of the same pitch would cut the second one off.
      samePitch = samePitch + 1
    else
      local newEnd = nextNote.startPpq + overlap
      if newEnd >= nextNote.endPpq then
        newEnd = nextNote.startPpq + (nextNote.endPpq - nextNote.startPpq) / 2
      end
      A.setEnd(take, n, newEnd)
      slid = slid + 1
    end
  end
end
reaper.MIDI_Sort(take)
reaper.Undo_EndBlock("Access808: slide selected notes into next note", -1)

if selectedCount == 0 then
  A.say("No notes selected.", true)
  return
end

local message = A.plural(slid, "note") .. " now slide into the next note."
if lastNote > 0 then message = message .. " " .. A.plural(lastNote, "note") .. " skipped, no note after it." end
if samePitch > 0 then message = message .. " " .. A.plural(samePitch, "note") .. " skipped, next note has the same pitch." end
A.say(message)
