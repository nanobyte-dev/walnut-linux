-- ps3bench: the Luanti benchmark of the PS3 game suite (games/bench/recipes/luanti.sh), installed
-- as a world mod. A server-side mod can't count rendered frames, so it only makes every run show
-- the same thing: the player holds still at the start while the first map blocks are generated and
-- meshed (ps3bench_warmup seconds), then flies a fixed path (ps3bench_seconds: a straight line north
-- looking down at the terrain, then a full turn in place), then the game shuts down. The fps come
-- from the Gallium HUD between the "path start" and "path end" lines this mod logs.
-- Positions are set on every server step (physics off, so the client doesn't move in between):
-- the path depends on time only, not on the frame rate.

local warmup = tonumber(core.settings:get("ps3bench_warmup")) or 15
local seconds = tonumber(core.settings:get("ps3bench_seconds")) or 45
local length = 240		-- nodes flown north
local straight = 0.75		-- share of the path spent flying straight; the rest is the turn

local name, alt
local t, started, ended = 0, false, false

local function log(msg)
	core.log("action", "ps3bench: " .. msg)
end

-- the pose at s seconds into the path: position, yaw, pitch (positive looks down)
local function pose(s)
	local a = straight * seconds
	if s < a then
		return {x = 0, y = alt, z = length * s / a}, 0, 0.45
	end
	local u = math.min((s - a) / (seconds - a), 1)
	return {x = 0, y = alt, z = length}, 2 * math.pi * u, 0.3
end

local function place(player, s)
	local pos, yaw, pitch = pose(s)
	player:set_pos(pos)
	player:set_look_horizontal(yaw)
	player:set_look_vertical(pitch)
end

core.register_on_joinplayer(function(player)
	name = player:get_player_name()
	-- above the ground at the start (the mapgen's own estimate; nil over water or cliffs)
	alt = (core.get_spawn_level(0, 0) or 10) + 25
	player:set_physics_override({speed = 0, jump = 0, gravity = 0})
	place(player, 0)
	log(string.format("joined, altitude %d, warmup %d s, path %d s", alt, warmup, seconds))
end)

core.register_globalstep(function(dtime)
	if not name or ended then return end
	local player = core.get_player_by_name(name)
	if not player then return end
	t = t + dtime
	if t < warmup then
		place(player, 0)
		return
	end
	if not started then
		started = true
		log("path start")
	end
	local s = t - warmup
	place(player, math.min(s, seconds))
	if s >= seconds then
		ended = true
		log("path end")
		core.request_shutdown("ps3bench done", false, 0)
	end
end)
