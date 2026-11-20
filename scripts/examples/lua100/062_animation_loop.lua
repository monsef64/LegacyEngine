-- 062: animation loop
-- Setup: Animation with a valid assigned .leanim clip.
-- Attach this script to one object using its LegacyScript component.

function onStart()
    local animation = self:getComponent(Animation)
    if animation and animation.clip ~= "" then animation.loop = true; animation:play() end
end
