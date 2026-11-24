-- Access808: set how far "Slide selected notes into next note" overlaps the
-- next note. Type a note division: 64 means a 1/64 note, 32 means a 1/32 note.
-- The overlap length does not change the sound of the slide; the glide always
-- starts when the next note starts. It is stored for all projects.

local scriptPath = debug.getinfo(1, "S").source:match("^@(.*[\\/])")
local A = dofile(scriptPath .. "Access808_common.lua")

local current = A.getOverlapDivision()
local ok, answer = reaper.GetUserInputs("Access808 slide overlap", 1,
  "Overlap as note division (64 = 1/64 note):", tostring(current))
if not ok then return end

local div = tonumber(answer)
if div == nil or div < 1 or div > 256 then
  A.say("Not changed. Type a number from 1 to 256, for example 64.", true)
  return
end

div = math.floor(div)
A.setOverlapDivision(div)
A.say("Slide overlap set to a 1/" .. div .. " note.")
