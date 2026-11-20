-- 029: gamepad move
-- Setup: Connect a controller. Attach to a non-physics object.
-- Attach this script to one object using its LegacyScript component.

function onUpdate(dt)
    local stick = getGamepadStick("left")
    if math.abs(stick.x) + math.abs(stick.y) > 0.2 then
        self:translate(stick.x * dt * 3, 0, stick.y * dt * 3)
    end
end
