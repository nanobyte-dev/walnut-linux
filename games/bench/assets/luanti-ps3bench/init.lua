-- ps3bench: the Luanti benchmark of the PS3 game suite (games/bench/recipes/luanti.sh), installed
-- as a world mod. A server-side mod can't count rendered frames, so it only makes every run show
-- the same thing: the player holds still at the start while the first map blocks are generated and
-- meshed (ps3bench_warmup seconds), then flies a fixed path (ps3bench_seconds: a straight line north
-- looking down at the terrain, then a full turn in place), then the game shuts down. The fps come
-- from the Gallium HUD between the "path start" and "path end" lines this mod logs.
-- It also logs the memory of the whole game process every 5 s ("ps3bench: mem <label> ..."; the
-- built-in server runs inside the client's process, so VmRSS is the game's): the Lua heap of the
-- server's mods in KiB, VmRSS/RssAnon/RssFile/VmSwap/VmHWM from /proc/self/status. Reading /proc
-- needs the insecure environment, so the bench config lists this mod in secure.trusted_mods;
-- without it only the Lua heap is logged. The recipe's parse() turns these lines into bench.txt keys.
-- Positions are set on every server step (physics off, so the client doesn't move in between):
-- the path depends on time only, not on the frame rate.
-- ps3bench_loops (default 1): the path flown that many times, back to its start each time, a
-- "ps3bench: path loop <k>" line at the start of each after the first; "path end" after the last.

local warmup = tonumber(core.settings:get("ps3bench_warmup")) or 15
local seconds = tonumber(core.settings:get("ps3bench_seconds")) or 45
local loops = math.max(1, tonumber(core.settings:get("ps3bench_loops")) or 1)
local loop = 0			-- the loop being flown, from 0
local length = 240		-- nodes flown north
local straight = 0.75		-- share of the path spent flying straight; the rest is the turn

local ie = core.request_insecure_environment()	-- at load time; nil unless trusted

local name, alt
local t, started, ended = 0, false, false
local mem_timer = 0

local function log(msg)
	core.log("action", "ps3bench: " .. msg)
end

-- one "mem" line: label is start/tick/end; every number in KiB
local function log_mem(label)
	local f = {string.format("lua=%d", collectgarbage("count"))}
	if ie then
		local h = ie.io.open("/proc/self/status", "r")
		if h then
			for line in h:lines() do
				local k, v = line:match("^(%a+):%s+(%d+) kB")
				if k == "VmRSS" or k == "RssAnon" or k == "RssFile" or k == "VmSwap" or k == "VmHWM" then
					f[#f + 1] = k:lower() .. "=" .. v
				end
			end
			h:close()
		end
	end
	log("mem " .. label .. " " .. table.concat(f, " "))
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
	mem_timer = mem_timer + dtime
	if mem_timer >= 5 then
		mem_timer = 0
		log_mem("tick")
	end
	if t < warmup then
		place(player, 0)
		return
	end
	if not started then
		started = true
		log("path start")
		log_mem("start")
	end
	local s = t - warmup
	local k = math.min(math.floor(s / seconds), loops - 1)
	if k > loop then
		loop = k
		log(string.format("path loop %d", k + 1))
	end
	s = s - k * seconds
	place(player, math.min(s, seconds))
	if s >= seconds then
		ended = true
		log("path end")
		log_mem("end")
		core.request_shutdown("ps3bench done", false, 0)
	end
end)
