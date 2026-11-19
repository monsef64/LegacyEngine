-- Vehicle controller: attach to the ROOT created with Create > Vehicle.
-- Play, then focus the Game view. Do not also attach vehicle_basic.ls.
-- W: forward / brake while reversing. S: reverse / brake while moving forward.
-- A/D: steer. Space: service brake. Left Shift: rear handbrake.
-- Uses Vehicle physics; never teleports the chassis or sets its velocity.

local vehicle
local steering = 0

-- Feel settings (not vehicle physics tuning).
local steeringRate = 3.0       -- normalized steering units per second
local steeringReturnRate = 5.0
local directionChangeSpeed = 0.5 -- m/s: brake before changing direction
local highSpeedSteering = 0.4 -- steering fraction at/above 100 km/h

local function approach(value, target, step)
    if value < target then return math.min(value + step, target) end
    return math.max(value - step, target)
end

function onStart()
    vehicle = self:getComponent(Vehicle) or self:addComponent(Vehicle)
    steering = 0
    vehicle.throttle = 0
    vehicle.steering = 0
    vehicle.brake = 0
    vehicle.handbrake = false
    -- Existing mass/suspension/grip settings are preserved.
end

function onFixedUpdate(dt)
    -- Reacquire safely if the component is removed/replaced during Play.
    vehicle = self:getComponent(Vehicle)
    if not vehicle then return end

    local forward = isKeyDown("W")
    local reverse = isKeyDown("S")
    local footBrake = isKeyDown("Space")
    local speed = vehicle.speed -- signed forward speed, metres per second
    local throttle, brake = 0, 0

    if footBrake or (forward and reverse) then
        brake = 1
    elseif forward then
        if speed < -directionChangeSpeed then brake = 1
        else throttle = 1 end
    elseif reverse then
        if speed > directionChangeSpeed then brake = 1
        else throttle = -1 end
    end

    local targetSteering = 0
    if isKeyDown("A") then targetSteering = targetSteering - 1 end
    if isKeyDown("D") then targetSteering = targetSteering + 1 end
    local speedFraction = math.min(math.abs(speed) * 3.6 / 100, 1)
    targetSteering = targetSteering * (1 - (1 - highSpeedSteering) * speedFraction)
    local rate = targetSteering == 0 and steeringReturnRate or steeringRate
    steering = approach(steering, targetSteering, rate * math.max(dt, 0))

    vehicle.throttle = throttle
    vehicle.steering = steering
    vehicle.brake = brake
    vehicle.handbrake = isKeyDown("Left Shift")
    -- Inputs are normalized commands: only smoothing, NOT throttle/braking,
    -- uses dt. Releasing W/S coasts; the Vehicle's existing idle hold handles rest.
end
