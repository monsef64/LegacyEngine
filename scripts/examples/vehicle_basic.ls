-- Attach to the root created by Create > Vehicle. Place it about 1.2 m above
-- a wide Plane/terrain with a Collider, and provide a Camera with a clear view.
-- Play and focus Game: W/S accelerate/reverse, A/D steer, Space brake,
-- Left Shift rear handbrake. This script never teleports the chassis.
local vehicle

function onStart()
    vehicle = self:getComponent(Vehicle) or self:addComponent(Vehicle)
    -- addComponent only supplies MISSING dependencies; an existing RigidBody
    -- keeps its mass. The default suspension is tuned around 1000 kg.
end

function onFixedUpdate(dt)
    local throttle, steering = 0, 0
    if isKeyDown("W") then throttle = throttle + 1 end
    if isKeyDown("S") then throttle = throttle - 1 end
    if isKeyDown("A") then steering = steering - 1 end
    if isKeyDown("D") then steering = steering + 1 end
    vehicle.throttle = throttle
    vehicle.steering = steering
    vehicle.brake = isKeyDown("Space") and 1 or 0
    vehicle.handbrake = isKeyDown("Left Shift")
    -- Inputs are normalized commands, NOT displacements: don't multiply by dt.
    -- Read-only: vehicle.speed (signed m/s), vehicle.groundedWheels (0..4),
    -- vehicle.isGrounded, vehicle.wheelCompression (copy, FL/FR/RL/RR).
end
