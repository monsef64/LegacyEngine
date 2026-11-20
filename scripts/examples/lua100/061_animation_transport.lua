-- 061: animation transport
-- Setup: Animation with assigned .leanim clip. P plays; S stops/rewinds.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Animation)
end
function onUpdate(dt)
    if not c then return end
    if isKeyPressed("P") then c:play() end
    if isKeyPressed("S") then c:stop() end
end
