-- 052: audio pause resume
-- Setup: AudioSource with clip and Play on Start. Space pauses/resumes.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(AudioSource)
end
local paused = false
function onUpdate(dt)
    if c and isKeyPressed("Space") then
        paused = not paused
        if paused then c:pause() else c:resume() end
    end
end
