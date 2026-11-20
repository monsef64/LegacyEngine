# 100 Lua examples

These are independent attached-object scripts, not one combined program. Copy a chosen `.lua` into your project Assets and attach it using the LegacyScript component. Press Play and focus Game for keyboard input. Run one movement/physics driver per object. Setup requirements are repeated at the top of every file. Paths and coordinates in asset/navigation examples must be adapted to your project. No additional assets or RigidBodies are silently created unless the example says so.

Keyboard edge events work in both onUpdate and onFixedUpdate. Vehicle upright mode intentionally locks pitch/roll to world up; disable it for stunts. Audio/animation examples require clips configured as described. These are learning examples, not performance benchmarks or automated tests.

| # | Example | Setup |
|---|---|---|
| 001 | [set position](001_set_position.lua) | Attach to any object with Transform. Sets world position once. |
| 002 | [set rotation](002_set_rotation.lua) | Attach to a mesh. Angles are degrees, not radians. |
| 003 | [nonuniform scale](003_nonuniform_scale.lua) | Attach to a sphere or mesh; no RigidBody needed. |
| 004 | [constant motion](004_constant_motion.lua) | Attach to a non-physics object. Speed is metres/second. |
| 005 | [spin](005_spin.lua) | Attach to a decorative object without physics. |
| 006 | [bobbing](006_bobbing.lua) | Attach to a decorative object. Remembers its starting height. |
| 007 | [orbit origin](007_orbit_origin.lua) | Attach to a decorative object. Orbits world origin at radius 4. |
| 008 | [pulse scale](008_pulse_scale.lua) | Attach to a decorative object; no physics driver. |
| 009 | [look at target](009_look_at_target.lua) | Create an object named Target. Attach to another object. |
| 010 | [clamp height](010_clamp_height.lua) | Attach to a script-driven object, not a dynamic body. |
| 011 | [rename self](011_rename_self.lua) | Attach to any object. |
| 012 | [spawn empty](012_spawn_empty.lua) | Attach to an object. Creates one empty object on Play. |
| 013 | [follow target](013_follow_target.lua) | Create Target; attach to a non-physics follower. |
| 014 | [parent to target](014_parent_to_target.lua) | Create Target. Parents this object to it once. |
| 015 | [detach parent](015_detach_parent.lua) | Attach to a child object. |
| 016 | [toggle target](016_toggle_target.lua) | Create Target. Focus Game; press E to toggle Target activity. |
| 017 | [timed destroy](017_timed_destroy.lua) | Attach to an expendable object. Destroys it after three seconds. |
| 018 | [count meshes](018_count_meshes.lua) | Attach to any object; prints count once. |
| 019 | [inspect parent](019_inspect_parent.lua) | Attach to a child object. |
| 020 | [find child light](020_find_child_light.lua) | Attach to a parent with a PointLight descendant. Search includes self. |
| 021 | [key pressed](021_key_pressed.lua) | Focus Game. Prints once per Space press. |
| 022 | [key held](022_key_held.lua) | Focus Game. Demonstrates held state without log spam. |
| 023 | [key released](023_key_released.lua) | Focus Game; press then release E. |
| 024 | [arrow movement](024_arrow_movement.lua) | Attach to a non-physics object. Focus Game; use arrow keys. |
| 025 | [fixed step jump](025_fixed_step_jump.lua) | Add a dynamic RigidBody and Collider. Space jumps; no ground gate in this basic demo. |
| 026 | [cursor toggle](026_cursor_toggle.lua) | Focus Game. L locks/unlocks; Escape unlocks. |
| 027 | [mouse delta rotation](027_mouse_delta_rotation.lua) | Attach to a decorative mesh. Hold right mouse in Game to turn it. |
| 028 | [named button](028_named_button.lua) | Define an Interact button in Project > Input Settings. Focus Game. |
| 029 | [gamepad move](029_gamepad_move.lua) | Connect a controller. Attach to a non-physics object. |
| 030 | [smooth acceleration](030_smooth_acceleration.lua) | Attach to a non-physics object. Hold W in Game. |
| 031 | [toggle gravity](031_toggle_gravity.lua) | Dynamic RigidBody + Collider. G toggles gravity. |
| 032 | [configure mass](032_configure_mass.lua) | Add a RigidBody first. Sets mass in kilograms. |
| 033 | [launch impulse](033_launch_impulse.lua) | Dynamic body + Collider. One launch at Play start. |
| 034 | [held force](034_held_force.lua) | Dynamic body + Collider. Hold F; force is applied each fixed step. |
| 035 | [limit velocity](035_limit_velocity.lua) | Dynamic body + Collider. Caps total speed at 8 metres/second. |
| 036 | [stop body](036_stop_body.lua) | Dynamic body. Press B to clear velocity and queued forces. |
| 037 | [toggle trigger](037_toggle_trigger.lua) | Attach to a Collider. T switches solid/sensor behavior. |
| 038 | [trigger counter](038_trigger_counter.lua) | Attach to a trigger Collider; let another Collider enter/exit. |
| 039 | [collision log](039_collision_log.lua) | Attach to a dynamic body with Collider; drop onto a floor. |
| 040 | [slippery surface](040_slippery_surface.lua) | Attach to a floor with Collider. No RigidBody needed on the floor. |
| 041 | [material color](041_material_color.lua) | Attach to a MeshRenderer. Uses instance overrides, not asset edits. |
| 042 | [color cycle](042_color_cycle.lua) | Attach to a visible mesh. |
| 043 | [checkerboard](043_checkerboard.lua) | Attach to a primitive MeshRenderer. |
| 044 | [specular material](044_specular_material.lua) | Attach to a MeshRenderer lit by a light. |
| 045 | [transparent shader](045_transparent_shader.lua) | Attach to a MeshRenderer. Choose opacity separately in its material. |
| 046 | [texture tiling](046_texture_tiling.lua) | Attach to a MeshRenderer with an assigned texture. |
| 047 | [pulsing light](047_pulsing_light.lua) | Attach to a dynamic PointLight. |
| 048 | [spot cone](048_spot_cone.lua) | Attach to a dynamic SpotLight. |
| 049 | [light switch](049_light_switch.lua) | Dynamic PointLight. Press E in Game to switch it. |
| 050 | [reset material](050_reset_material.lua) | MeshRenderer with instance overrides. R restores its asset appearance. |
| 051 | [audio transport](051_audio_transport.lua) | AudioSource with an assigned WAV/MP3. P plays, S stops. |
| 052 | [audio pause resume](052_audio_pause_resume.lua) | AudioSource with clip and Play on Start. Space pauses/resumes. |
| 053 | [audio fade](053_audio_fade.lua) | AudioSource with assigned clip. F fades in, G fades out. |
| 054 | [audio pitch](054_audio_pitch.lua) | Playing AudioSource. Hold W to raise pitch smoothly. |
| 055 | [audio pan](055_audio_pan.lua) | Playing AudioSource; this demo uses 2D panning. |
| 056 | [audio 2d](056_audio_2d.lua) | AudioSource with assigned clip. Configure as non-spatial music. |
| 057 | [audio 3d](057_audio_3d.lua) | AudioSource with clip; active AudioListener needed nearby. |
| 058 | [audio mute](058_audio_mute.lua) | Playing AudioSource. M toggles mute. |
| 059 | [audio loop](059_audio_loop.lua) | AudioSource with assigned clip. |
| 060 | [audio zone fade](060_audio_zone_fade.lua) | Attach to a trigger Collider. Put AudioSource on object Music, assign a clip. |
| 061 | [animation transport](061_animation_transport.lua) | Animation with assigned .leanim clip. P plays; S stops/rewinds. |
| 062 | [animation loop](062_animation_loop.lua) | Animation with a valid assigned .leanim clip. |
| 063 | [animation pause](063_animation_pause.lua) | Playing Animation. Space toggles pause/resume. |
| 064 | [animation finish](064_animation_finish.lua) | Non-looping Animation with Play on Start. |
| 065 | [animation crossfade](065_animation_crossfade.lua) | Create Animations/Run.leanim compatible with the assigned current clip; R blends to it. |
| 066 | [particle preset](066_particle_preset.lua) | Attach to any object. Adds a ParticleSystem if missing. |
| 067 | [particle burst](067_particle_burst.lua) | ParticleSystem with emission disabled in Inspector. Space emits 25 particles. |
| 068 | [particle pause](068_particle_pause.lua) | Playing ParticleSystem. Space toggles pause. |
| 069 | [particle restart](069_particle_restart.lua) | ParticleSystem. R restarts simulation; C clears existing particles. |
| 070 | [collision particles](070_collision_particles.lua) | Dynamic Collider + ParticleSystem with normal emission disabled. |
| 071 | [agent destination](071_agent_destination.lua) | NavMeshAgent on baked navmesh. Set reachable coordinates for your level. |
| 072 | [agent command](072_agent_command.lua) | Baked navmesh; set a reachable destination below. Space queues movement. |
| 073 | [agent follow](073_agent_follow.lua) | Baked navmesh and Target object on it. Requests a route twice per second. |
| 074 | [agent patrol](074_agent_patrol.lua) | Baked navmesh; adjust these two points to reachable positions. |
| 075 | [agent pause](075_agent_pause.lua) | Agent following a route. Space pauses/resumes without clearing it. |
| 076 | [agent sprint](076_agent_sprint.lua) | Moving agent; hold Left Shift in Game. |
| 077 | [agent arrival](077_agent_arrival.lua) | Agent controlled by another destination script or command. |
| 078 | [obstacle toggle](078_obstacle_toggle.lua) | Obstacle on baked navmesh, separate from agents. E toggles avoidance. |
| 079 | [obstacle size](079_obstacle_size.lua) | Obstacle on baked plane. Dynamic box changes cause bounded agent repaths. |
| 080 | [agent status](080_agent_status.lua) | Agent on a baked navmesh. Logs status once per second. |
| 081 | [vehicle keyboard](081_vehicle_keyboard.lua) | Attach to Create > Vehicle root above a collidable floor. WASD, Space, Shift. |
| 082 | [vehicle cruise](082_vehicle_cruise.lua) | Vehicle root on a wide floor. Simple 8 m/s cruise controller. |
| 083 | [vehicle reverse](083_vehicle_reverse.lua) | Vehicle root. Hold S for reverse; release to coast. |
| 084 | [vehicle speed governor](084_vehicle_speed_governor.lua) | Vehicle root, wide floor. Demonstrates throttle cutoff plus service braking. |
| 085 | [vehicle parking brake](085_vehicle_parking_brake.lua) | Vehicle root. Space toggles parking brake; W drives when released. |
| 086 | [vehicle boost](086_vehicle_boost.lua) | Vehicle root. W drives; hold Left Shift to increase engine force. |
| 087 | [vehicle smooth steering](087_vehicle_smooth_steering.lua) | Vehicle root. A/D steering eases toward input; W accelerates. |
| 088 | [vehicle telemetry](088_vehicle_telemetry.lua) | Vehicle root. Can run alongside a separate driving script. |
| 089 | [vehicle airborne cutoff](089_vehicle_airborne_cutoff.lua) | Vehicle root. Cuts engine command when all wheels lose contact. |
| 090 | [vehicle upright toggle](090_vehicle_upright_toggle.lua) | Vehicle root. U toggles hard upright lock. Turning it off permits flips. |
| 091 | [health damage](091_health_damage.lua) | Attach to a dynamic Collider. Each new collision deals 10 damage. |
| 092 | [pickup counter](092_pickup_counter.lua) | Attach to a player trigger. Name collectible objects Coin. |
| 093 | [ability cooldown](093_ability_cooldown.lua) | Focus Game. E activates at most once every two seconds. |
| 094 | [stopwatch](094_stopwatch.lua) | Space starts/stops the timer; R resets it. |
| 095 | [countdown hide](095_countdown_hide.lua) | Create a Target object. After five seconds, disables Target once. |
| 096 | [sliding door](096_sliding_door.lua) | Attach to a non-physics door. E toggles a smooth 3-metre opening. |
| 097 | [day night rotation](097_day_night_rotation.lua) | Attach to a dynamic DirectionalLight. One revolution per two minutes. |
| 098 | [proximity indicator](098_proximity_indicator.lua) | Attach to a mesh. Create Target; color becomes green within 3 metres. |
| 099 | [fall respawn](099_fall_respawn.lua) | Dynamic body + Collider. Falling below Y=-10 resets to its starting point. |
| 100 | [timed spawn wave](100_timed_spawn_wave.lua) | Attach to an empty object. Spawns five static cubes, one each second. |
