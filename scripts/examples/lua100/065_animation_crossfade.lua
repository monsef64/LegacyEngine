-- 065: animation crossfade
-- Setup: Create Animations/Run.leanim compatible with the assigned current clip; R blends to it.
-- Attach this script to one object using its LegacyScript component.

local c
function onStart()
    c = self:getComponent(Animation)
end
function onUpdate(dt)
    if c and isKeyPressed("R") then c:crossFade("Animations/Run.leanim", 0.3) end
end
