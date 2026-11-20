-- 094: stopwatch
-- Setup: Space starts/stops the timer; R resets it.
-- Attach this script to one object using its LegacyScript component.

local elapsed, running = 0, false
function onUpdate(dt)
    if isKeyPressed("Space") then running = not running; debug.log("Seconds:", elapsed) end
    if isKeyPressed("R") then elapsed = 0 end
    if running then elapsed = elapsed + dt end
end
