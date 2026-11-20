-- 051: audio transport
-- Setup: AudioSource with an assigned WAV/MP3. P plays, S stops.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(AudioSource)
end
function onUpdate(dt)
    if not c then return end
    if isKeyPressed("P") then c:play() end
    if isKeyPressed("S") then c:stop() end
end
