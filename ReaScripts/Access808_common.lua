-- Shared helpers for the Access808 slide scripts. Not an action by itself.
--
-- Access808 slides when a note starts while the previous note is still held
-- (overlap / legato). These scripts create or remove that overlap for you, so
-- slides can be written without editing note ends by ear.

local M = {}

M.section = "Access808"
M.defaultDivision = 64 -- overlap of a 1/64 note

-- Speak through OSARA when it is installed. Errors also show a message box
-- when OSARA is not available.
function M.say(message, isError)
  if reaper.osara_outputMessage then
    reaper.osara_outputMessage(message)
  elseif isError then
    reaper.ShowMessageBox(message, "Access808", 0)
  end
end

function M.getOverlapDivision()
  local v = tonumber(reaper.GetExtState(M.section, "overlapDivision"))
  if v == nil or v < 1 or v > 256 then return M.defaultDivision end
  return v
end

function M.setOverlapDivision(div)
  reaper.SetExtState(M.section, "overlapDivision", tostring(div), true)
end

-- The MIDI take to work on: the active MIDI editor's take, otherwise the
-- active take of the first selected item.
function M.getTake()
  local editor = reaper.MIDIEditor_GetActive()
  if editor then
    local take = reaper.MIDIEditor_GetTake(editor)
    if take and reaper.TakeIsMIDI(take) then return take end
  end
  local item = reaper.GetSelectedMediaItem(0, 0)
  if item then
    local take = reaper.GetActiveTake(item)
    if take and reaper.TakeIsMIDI(take) then return take end
  end
  return nil
end

function M.ppqPerQuarter(take)
  local qn = reaper.MIDI_GetProjQNFromPPQPos(take, 0)
  return reaper.MIDI_GetPPQPosFromProjQN(take, qn + 1) - reaper.MIDI_GetPPQPosFromProjQN(take, qn)
end

-- All notes, with their index, sorted by start time.
function M.readNotes(take)
  local _, noteCount = reaper.MIDI_CountEvts(take)
  local notes = {}
  for i = 0, noteCount - 1 do
    local _, selected, muted, startPpq, endPpq, chan, pitch, vel = reaper.MIDI_GetNote(take, i)
    notes[#notes + 1] = { index = i, selected = selected, muted = muted, startPpq = startPpq,
                          endPpq = endPpq, chan = chan, pitch = pitch, vel = vel }
  end
  table.sort(notes, function(a, b)
    if a.startPpq == b.startPpq then return a.index < b.index end
    return a.startPpq < b.startPpq
  end)
  return notes
end

-- The first unmuted note that starts after note n (notes sorted by start).
function M.nextNote(notes, position)
  local n = notes[position]
  for j = position + 1, #notes do
    local m = notes[j]
    if m.startPpq > n.startPpq and not m.muted then return m end
  end
  return nil
end

function M.setEnd(take, note, endPpq)
  reaper.MIDI_SetNote(take, note.index, nil, nil, nil, endPpq, nil, nil, nil, true)
end

function M.plural(n, word)
  if n == 1 then return "1 " .. word end
  return tostring(n) .. " " .. word .. "s"
end

return M
