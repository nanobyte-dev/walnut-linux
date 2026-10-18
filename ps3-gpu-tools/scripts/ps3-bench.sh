#!/bin/bash
# ps3-bench.sh: measurements from a quiet PS3 (run as tibi over SSH; uses sudo).
#   ps3-bench.sh quiet                     stop the desktop, drop caches, empty the swaps
#   ps3-bench.sh run LABEL -- cmd...       quiet, then cmd with memory/VRAM samples every 0.5 s
#   ps3-bench.sh x LABEL -- cmd...         the same, cmd as the only client of a bare X on vt1
#   ps3-bench.sh snap LABEL                one full snapshot into the current run directory
#   ps3-bench.sh desktop                   end of the session: desktop back, game config restored
# Results: /var/tmp/ps3-bench/<UTC stamp>-<label>/ (out.log with $EPOCHREALTIME stamps, samples,
# snapshots); written to /dev/shm while the command runs, so the USB disk stays out of it.
# The first quiet of a session takes the bench lock (no deploys meanwhile); desktop releases it.
# Every quiet also pauses the systemd timers of periodic jobs (BENCH_TIMERS: apt-daily, apt-daily-
# upgrade, man-db, e2scrub_all, fstrim, logrotate, dpkg-db-backup) and waits for a job they started
# (stopping the harmless ones after 120 s): they stay paused for the whole session and come back at
# desktop (a session never ended gets them back at the next boot: stop is not disable). Not after
# each run: apt-daily's timer is Persistent, so starting it again would fire the missed run into the
# next measurement. bench.txt and result get timers_paused=.
# Games and their config files are the business of games/bench (its client.sh backs up and
# restores what each game rewrites).
# A watchdog stops the command on a "PS3 Thermal Alert" (Linux can't read temperatures).
# BENCH_TIMEOUT (default 600 s) bounds every command. BENCH_HUD=1: per-second fps from the Gallium
# HUD into the run directory. BENCH_NOVSWAP=1: VRAM swap off for the run (the first run of anything
# that might wedge the GPU); desktop turns it back on. BENCH_STALLS=1: ps3-stallrec records the
# game's main thread and the memory and disk counters every BENCH_STALLS_MS (default 10) ms into
# stalls.rec, for games/bench/stalls.py (why frames stall, docs/STALLS.md). BENCH_PERF=1: perf
# record (call graphs, BENCH_PERF_HZ samples a second, default 499) of the game's threads, Xorg and
# kswapd0, started disabled once client.sh's .pid appears and switched on and off by client.sh over
# the measured window through the fifo $BENCH_PERF_CTL; perf.data goes to the disk, not /dev/shm,
# and into the run directory at the end (docs/PROFILING.md; games/bench/perf-post.sh reads it).
# BENCH_PERF_ALL=1 adds a system-wide perf without call graphs (perf-sys.data). The command sees the
# run directory as $BENCH_DIR.
# The A/B switches of docs/STALLS.md "The recorder removes stalls" (ps3-waker: ps3-gpu-tools 0.399+):
# BENCH_STALLS_CPU=LIST pins the recorder (taskset), BENCH_STALLS_NOWRITE=1 samples without writing
# (stallrec-nowrite.rec, trailer only). BENCH_WAKER=MS: ps3-waker alongside, BENCH_WAKER_MODE sleep
# (default: a bare periodic sleep, no reads) or kick (a cross-CPU wakeup to each CPU of
# BENCH_WAKER_CPU every MS), BENCH_WAKER_CPU=LIST pins the sleeper / picks the kicked CPUs (waker.txt).
# BENCH_PROBE=1: pinned SCHED_FIFO sleepers time how late each CPU's timers fire, every
# BENCH_PROBE_MS (default 20) on BENCH_PROBE_CPU (default all), into probe.txt. bench.txt gets
# harness= (these switches with BENCH_STALLS and BENCH_PERF as one tag; report.py shows it with the
# variant).
{
set -u
LOCK=/dev/shm/ps3-bench.lock
OUT=/var/tmp/ps3-bench

die() { echo "!! $*" >&2; exit 1; }
kb() { awk -v k="$1:" '$1 == k { print $2 }' /proc/meminfo; }
mmfile() { sudo sh -c 'ls /sys/kernel/debug/dri/*/ps3rsx_mm 2>/dev/null | head -1'; }

session_start() {
	[ -e $LOCK ] && return
	{ echo "started $(date -u +%FT%TZ) by $$"; uname -r
	  echo "SPEs: $(ls -d /sys/devices/system/spu/spu[0-9]* 2>/dev/null | wc -l)"; } > $LOCK
	mkdir -p $OUT
	echo ">> bench session: $(tr '\n' ' ' < $LOCK)"
}

stop_desktop() {
	if systemctl is-active -q getty@tty1 || pidof Xorg >/dev/null; then
		echo '>> stopping the desktop'
		sudo systemctl stop getty@tty1
		for s in $(loginctl list-sessions --no-legend | awk '$4=="tty1" || $5=="tty1" {print $1}'); do
			sudo loginctl terminate-session "$s"
		done
	fi
	for i in $(seq 15); do pidof Xorg xinit >/dev/null || break; sleep 1; done
	if pidof xinit Xorg >/dev/null; then
		sudo pkill -TERM -x xinit; sleep 3
		sudo pkill -KILL -x xinit; sudo pkill -KILL -x Xorg; sleep 1
	fi
	pidof Xorg xinit >/dev/null && die "X still running"
	if [ -e /tmp/.X0-lock ]; then	# a lock left by an unclean stop: xinit :0 would refuse
		p=$(tr -d ' ' < /tmp/.X0-lock)
		kill -0 "$p" 2>/dev/null || sudo rm -f /tmp/.X0-lock /tmp/.X11-unix/X0
	fi
	systemctl is-active -q getty@tty1 && die "getty@tty1 came back"
}

drop_caches() { sync; echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null; }

# Swap off and on again, one device at a time; restored explicitly: ps3vswap0 with a full discard
# (frees its stale VRAM slots), never by restarting ps3-vswap/ps3-swap (backing set, partition
# held exclusively: the restart fails and leaves no swap)
empty_swaps() {
	local used zram need avail dev prio devs=()
	used=$(( $(kb SwapTotal) - $(kb SwapFree) ))
	if [ "$used" -lt 8192 ]; then echo ">> swap: ${used} kB used, left as is"; return; fi
	cat /sys/block/ps3vswap0/stats 2>/dev/null | sed 's/^/   vswap before: /'
	# active swaps, zram first (its pages are in RAM already), then VRAM, then the rest
	while read -r dev prio; do devs+=("$dev:$prio"); done < <(awk 'NR>1 {
		o = $1 ~ /zram/ ? 0 : $1 ~ /ps3vswap/ ? 1 : 2; print o, $1, $5 }' /proc/swaps | sort -n | cut -d' ' -f2-)
	for d in "${devs[@]}"; do
		dev=${d%:*}
		used=$(( $(kb SwapTotal) - $(kb SwapFree) ))
		zram=$(( $(awk '{print $3}' /sys/block/zram0/mm_stat 2>/dev/null || echo 0) / 1024 ))
		need=$(( used - zram )); avail=$(kb MemAvailable)
		[ $need -gt $(( avail - 49152 )) ] && die "swapoff $dev: ${need} kB to bring back, ${avail} kB available"
		echo ">> swapoff $dev (${used} kB swapped)"
		sudo timeout -s INT 180 swapoff "$dev" || die "swapoff $dev failed or timed out (the kernel keeps it on)"
	done
	for d in "${devs[@]}"; do
		dev=${d%:*}; prio=${d#*:}
		case $dev in
		*ps3vswap*) sudo swapon -p "$prio" --discard "$dev" ;;
		*) sudo swapon -p "$prio" "$dev" ;;
		esac || die "swapon $dev failed"
	done
	cat /sys/block/ps3vswap0/stats 2>/dev/null | sed 's/^/   vswap after: /'
	if [ -e /sys/block/ps3vswap0/stats ]; then
		# one page may stay: the swap header (page 0), which a discard never covers
		awk '/pages in VRAM/ { exit $4 > 1 }' /sys/block/ps3vswap0/stats ||
			die "VRAM swap still holds pages after the discard"
	fi
}

vswap_on() {	# the VRAM swap back on if it has its backing but is off
	if [ -e /sys/block/ps3vswap0/backing ] && [ -n "$(cat /sys/block/ps3vswap0/backing)" ] &&
	   ! grep -q '^/dev/ps3vswap0 ' /proc/swaps; then
		sudo swapon -p 300 --discard /dev/ps3vswap0 && echo '>> VRAM swap on again'
	fi
}

# Periodic jobs paused for the session (see the top); the list of what was paused in $TIMERS
TIMERS=/dev/shm/ps3-bench.timers
BENCH_TIMERS=${BENCH_TIMERS-apt-daily.timer apt-daily-upgrade.timer man-db.timer e2scrub_all.timer fstrim.timer logrotate.timer dpkg-db-backup.timer}
STOPPABLE=" apt-daily.service man-db.service logrotate.service fstrim.service dpkg-db-backup.service "
running() { case $(systemctl is-active "$1" 2>/dev/null) in active|activating|deactivating|reloading) return 0 ;; esac; return 1; }
pause_timers() {
	local t s i paused=
	for t in $BENCH_TIMERS; do
		systemctl is-active -q "$t" || continue
		sudo systemctl stop "$t" && echo "$t" >> $TIMERS && paused="$paused $t"
	done
	[ -n "$paused" ] && echo ">> timers paused for the session:$paused"
	[ -e $TIMERS ] && sort -u -o $TIMERS $TIMERS
	for t in $BENCH_TIMERS; do	# a job one of them started, still running
		s=${t%.timer}.service
		running "$s" || continue
		echo ">> $s is running: waiting for it (120 s at most)"
		for i in $(seq 120); do running "$s" || break; sleep 1; done
		running "$s" || continue
		case $STOPPABLE in
		*" $s "*) echo ">> stopping $s"; sudo systemctl stop "$s" ;;
		*) die "$s still running after 120 s (not one to interrupt): try again later" ;;
		esac
	done
}
resume_timers() {
	[ -s $TIMERS ] || { rm -f $TIMERS; return 0; }
	sudo systemctl start $(cat $TIMERS) && echo ">> timers back: $(tr '\n' ' ' < $TIMERS)"
	rm -f $TIMERS
}

quiet() {
	session_start
	pause_timers
	stop_desktop
	drop_caches
	empty_swaps
	vswap_on	# a BENCH_NOVSWAP run earlier left it off
	if [ "${BENCH_NOVSWAP:-0}" = 1 ] && grep -q '^/dev/ps3vswap0 ' /proc/swaps; then
		echo '>> VRAM swap off for this run'
		sudo timeout -s INT 180 swapoff /dev/ps3vswap0 || die "swapoff /dev/ps3vswap0 failed"
	fi
	drop_caches
	echo ">> quiet: MemAvailable $(kb MemAvailable) kB, swap used $(( $(kb SwapTotal) - $(kb SwapFree) )) kB"
}

snap() {	# snap LABEL: everything, into $D/snap-LABEL.txt
	local f=$D/snap-$1.txt m
	m=$(mmfile)
	{ echo "== $1 $(date -u +%FT%T.%NZ) $EPOCHREALTIME"
	  grep -E '^(MemTotal|MemAvailable|MemFree|Cached|Shmem|SwapTotal|SwapFree|VmallocUsed):' /proc/meminfo
	  cat /proc/swaps
	  grep -E '^pswp(in|out) ' /proc/vmstat
	  echo "user.slice memory.current $(cat /sys/fs/cgroup/user.slice/memory.current 2>/dev/null)"
	  [ -e /sys/block/ps3vswap0/stats ] && cat /sys/block/ps3vswap0/stats
	  [ -n "$m" ] && sudo cat "$m"; } > "$f"
}

# The sampler and the thermal watchdog: one root shell, stopped by $D/stop. One line per 0.5 s:
# time, MemAvailable, SwapFree, pswpin, pswpout, GART backed (KiB), vswap in VRAM / on disk /
# written back, then each ps3rsx client as comm:VRAM:GART (KiB).
start_sampler() {	# start_sampler JOBPID
	sudo env D="$D" M="$(mmfile)" J="$1" bash -c '
		dmesg -W > "$D/dmesg.log" 2>&1 & w=$!
		trap "kill $w 2>/dev/null" EXIT
		while [ ! -e "$D/stop" ]; do
			if grep -qi "thermal alert" "$D/dmesg.log"; then
				echo "THERMAL ALERT at $EPOCHREALTIME: stopping the job" | tee "$D/THERMAL" > /dev/console
				kill -TERM -- -"$J" 2>/dev/null; sleep 5; kill -KILL -- -"$J" 2>/dev/null
				break
			fi
			awk -v t="$EPOCHREALTIME" '\''
				FILENAME == "/proc/meminfo" && $1 == "MemAvailable:" { a = $2 }
				FILENAME == "/proc/meminfo" && $1 == "SwapFree:" { s = $2 }
				FILENAME == "/proc/vmstat" && $1 == "pswpin" { pi = $2 }
				FILENAME == "/proc/vmstat" && $1 == "pswpout" { po = $2 }
				FILENAME != "/proc/meminfo" && FILENAME != "/proc/vmstat" {
					if (/^GART backed by memory:/) g = $5
					else if (/^swap: /) { vv = $2; vd = $6; vw = $9 }
					else if (/ handles, VRAM /) { c = c " " $1 ":" $5 ":" $8 }
				}
				END { printf "%s %s %s %s %s %s %s %s %s |%s\n", t, a, s, pi, po, g, vv, vd, vw, c }
			'\'' /proc/meminfo /proc/vmstat ${M:+"$M"} >> "$D/samples"
			sleep 0.5
		done' &
	SAMPLER=$!
}

# The stall recorder (BENCH_STALLS=1): one root process, the game found through client.sh's .pid,
# frames through the HUD's frame time dump, stopped by $D/stop like the sampler
STALLREC=
start_stallrec() {
	[ "${BENCH_STALLS:-0}" = 1 ] || return 0
	if ! command -v ps3-stallrec >/dev/null; then
		echo '!! BENCH_STALLS=1 but no ps3-stallrec (ps3-gpu-tools): recording nothing' >&2
		return 0
	fi
	local o=$D/stalls.rec w= pin=()
	# no-write mode: the reads without the record (a file stalls.py doesn't take for one)
	[ "${BENCH_STALLS_NOWRITE:-0}" = 1 ] && { o=$D/stallrec-nowrite.rec; w=-W; }
	[ -n "${BENCH_STALLS_CPU:-}" ] && pin=(taskset -c "$BENCH_STALLS_CPU")
	sudo "${pin[@]}" ps3-stallrec -o "$o" $w -i "${BENCH_STALLS_MS:-10}" -p "$D/.pid" \
		-f "$D/hud/frametime_(ms)" -s "$D/stop" -x Xorg -x kswapd0 2> "$D/stallrec.err" &
	STALLREC=$!
}

# ps3-waker (BENCH_WAKER=MS, BENCH_PROBE=1): periodic wakeups and nothing else, or how late each
# CPU's timers fire; root (nice -10 like the recorder, SCHED_FIFO for the probe), stopped by $D/stop
WAKER= PROBE=
start_waker() {
	if [ -n "${BENCH_WAKER:-}${BENCH_PROBE:-}" ] && ! command -v ps3-waker >/dev/null; then
		echo '!! BENCH_WAKER/BENCH_PROBE set but no ps3-waker (ps3-gpu-tools 0.399+): nothing started' >&2
		return 0
	fi
	if [ -n "${BENCH_WAKER:-}" ] && [ "$BENCH_WAKER" != 0 ]; then
		sudo ps3-waker -m "${BENCH_WAKER_MODE:-sleep}" -i "$BENCH_WAKER" \
			${BENCH_WAKER_CPU:+-c "$BENCH_WAKER_CPU"} -s "$D/stop" -o "$D/waker.txt" 2> "$D/waker.err" &
		WAKER=$!
	fi
	if [ "${BENCH_PROBE:-0}" = 1 ]; then
		sudo ps3-waker -m probe -i "${BENCH_PROBE_MS:-20}" ${BENCH_PROBE_CPU:+-c "$BENCH_PROBE_CPU"} \
			-s "$D/stop" -o "$D/probe.txt" 2> "$D/probe.err" &
		PROBE=$!
	fi
}
# the switches as one tag (bench.txt harness=, report.py's variant column); empty: none of them
harness() {
	local t=() nw=
	[ "${BENCH_STALLS_NOWRITE:-0}" = 1 ] && nw=nw
	[ "${BENCH_STALLS:-0}" = 1 ] && t+=("rec${BENCH_STALLS_MS:-10}${BENCH_STALLS_CPU:+c$BENCH_STALLS_CPU}$nw")
	[ -n "${BENCH_WAKER:-}" ] && [ "$BENCH_WAKER" != 0 ] &&
		t+=("wk${BENCH_WAKER_MODE:-sleep}${BENCH_WAKER}${BENCH_WAKER_CPU:+c$BENCH_WAKER_CPU}")
	[ "${BENCH_PROBE:-0}" = 1 ] && t+=("probe${BENCH_PROBE_MS:-20}${BENCH_PROBE_CPU:+c$BENCH_PROBE_CPU}")
	[ "${BENCH_PERF:-0}" = 1 ] && t+=(perf)
	local IFS=+
	echo "${t[*]}" | tr , _
}

# The profiler (BENCH_PERF=1, docs/PROFILING.md). perf.data is ~20 MB a run: written to the disk
# ($OUT/.perf-<run>, moved into the run directory at the end), not to /dev/shm where it would be
# RAM the game doesn't get. cpu-clock: perf_events has no driver for the Cell PMU. Call graphs by
# frame pointer: on ppc64 that is the ABI's stack back chain, there in every frame whatever the
# compiler flags, so no dwarf stack copies (8 KiB a sample). --buildid-mmap: the build ids come
# with the mmap events, no pass over the file at the end. The control fifos are made here, before
# the job starts, and exported for client.sh: it writes enable/disable at the window's ends.
PERFJOB= PD=
prep_perf() {
	unset BENCH_PERF_CTL
	[ "${BENCH_PERF:-0}" = 1 ] || return 0
	if ! command -v perf >/dev/null; then
		echo '!! BENCH_PERF=1 but no perf (linux-perf): profiling nothing' >&2
		return 0
	fi
	PD=$OUT/.perf-${D##*/}
	mkdir -p "$PD"
	mkfifo "$D/.perf.ctl" "$D/.perf.ack"
	BENCH_PERF_CTL="$D/.perf.ctl:$D/.perf.ack"
	if [ "${BENCH_PERF_ALL:-0}" = 1 ]; then
		mkfifo "$D/.perfsys.ctl" "$D/.perfsys.ack"
		BENCH_PERF_CTL="$BENCH_PERF_CTL $D/.perfsys.ctl:$D/.perfsys.ack"
	fi
	export BENCH_PERF_CTL
}
start_perf() {
	[ -n "$PD" ] || return 0
	sudo env D="$D" PD="$PD" HZ="${BENCH_PERF_HZ:-499}" ALL="${BENCH_PERF_ALL:-0}" bash -c '
		# the tick: a read timing out on a fifo nobody writes (no sleep forked every 0.25 s)
		mkfifo "$D/.perf.tick"; exec 9<> "$D/.perf.tick"; rm -f "$D/.perf.tick"
		for i in $(seq 2400); do [ -s "$D/.pid" ] && break; [ -e "$D/stop" ] && exit 0; read -r -t 0.25 -u 9; done
		read -r p < "$D/.pid" 2>/dev/null && [ -n "$p" ] || exit 0
		x=$(pidof -s Xorg); k=$(pgrep -x kswapd0 | head -1)
		readlink -f "/proc/$p/exe" > "$D/perf.exe"
		echo "game=$p xorg=$x kswapd0=$k hz=$HZ" > "$D/perf.targets"
		s=
		if [ "$ALL" = 1 ]; then	# no -g: system-wide call graphs hung the PS3 once (docs/PERF.md)
			perf record -q -e cpu-clock -F "$HZ" -a -k mono -N --buildid-mmap -D -1 \
				--control "fifo:$D/.perfsys.ctl,$D/.perfsys.ack" -o "$PD/perf-sys.data" 2> "$D/perf-sys.err" &
			s=$!
		fi
		perf record -q -e cpu-clock -F "$HZ" -g -k mono -N --buildid-mmap -D -1 \
			--control "fifo:$D/.perf.ctl,$D/.perf.ack" --proc-map-timeout=20000 \
			-p "$p${x:+,$x}${k:+,$k}" -o "$PD/perf.data" 2> "$D/perf.err" &
		r=$!
		echo "$r $s" > "$D/.perf.pid"	# client.sh reads their CPU time over the window
		# the mmaps of the load: -D -1 on running processes records none while disabled (perf keeps
		# its tracking event off until an exec that never comes, and "enable dummy:u" through the
		# fifo did not bring them either: perf 7.2.9), so libraries dlopened during the load were
		# [unknown]. So everything on, then cpu-clock off at once: the tracking event stays on,
		# the samples wait for client.sh. perf reads the fifo once it has the running processes
		# maps; the acks are read here, long before client.sh sends anything. perf.track: ok or not.
		exec 5<> "$D/.perf.ctl" 6<> "$D/.perf.ack"
		printf "enable\ndisable cpu-clock\n" >&5
		t=ok
		read -r -t 30 -u 6 a || t="no ack for enable"
		read -r -t 5 -u 6 a || { [ "$t" = ok ] && t=; t="${t:+$t; }no ack for disable cpu-clock"; }
		echo "$t" > "$D/perf.track"
		exec 5>&- 6>&-
		while kill -0 $r 2>/dev/null; do	# kswapd0 never exits: stopped by the stop file
			[ -e "$D/stop" ] && kill -INT $r $s 2>/dev/null
			read -r -t 0.5 -u 9
		done
		wait $r $s' &
	PERFJOB=$!
}

stop_sampler() {
	touch "$D/stop"; wait "$SAMPLER" 2>/dev/null
	if [ -n "$PERFJOB" ]; then	# perf writes its last buffers on SIGINT
		for i in $(seq 240); do kill -0 "$PERFJOB" 2>/dev/null || break; sleep 0.25; done
		kill "$PERFJOB" 2>/dev/null; wait "$PERFJOB" 2>/dev/null; PERFJOB=
	fi
	if [ -n "$STALLREC" ]; then	# it looks at the stop file every 10 samples
		for i in $(seq 30); do kill -0 "$STALLREC" 2>/dev/null || break; sleep 0.1; done
		kill "$STALLREC" 2>/dev/null; wait "$STALLREC" 2>/dev/null; STALLREC=
	fi
	for p in $WAKER $PROBE; do	# they look at the stop file every 100 ms
		for i in $(seq 30); do kill -0 "$p" 2>/dev/null || break; sleep 0.1; done
		kill "$p" 2>/dev/null; wait "$p" 2>/dev/null
	done
	WAKER= PROBE=
	rm -f "$D/stop"
}

# run_job LABEL cmd...: the command in its own process group, output stamped line by line
run_job() {
	local label=$1 rc t0 t1 h; shift
	D=/dev/shm/ps3-bench-$(date -u +%Y%m%dT%H%M%SZ)-$label
	mkdir -p "$D"
	printf '%q ' "$@" > "$D/cmd"; echo >> "$D/cmd"
	export BENCH_DIR=$D	# for the command: a place for its own output
	prep_perf
	if [ "${BENCH_HUD:-0}" = 1 ]; then	# per-second fps from the Gallium HUD, into $D/hud/fps
		mkdir -p "$D/hud"
		export GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_PERIOD=1 GALLIUM_HUD_DUMP_DIR=$D/hud
	fi
	snap pre
	t0=$EPOCHREALTIME
	# setsid: not a group leader here, so it execs, and bash's $$ is the new process group
	setsid bash -c 'echo $$ > "$BENCH_DIR/pgid"; exec timeout "$0" "$@"' "${BENCH_TIMEOUT:-600}" "$@" \
		< /dev/null 2>&1 | while IFS= read -r l; do printf '%s %s\n' "$EPOCHREALTIME" "$l"; done > "$D/out.log" &
	local pipe=$!
	for i in 1 2 3 4 5 6 7 8 9 10; do [ -s "$D/pgid" ] && break; sleep 0.1; done
	start_sampler "$(cat "$D/pgid" 2>/dev/null || echo 0)"
	start_stallrec
	start_waker
	start_perf
	wait $pipe; rc=$?
	t1=$EPOCHREALTIME
	stop_sampler
	snap post
	h=$(harness)
	[ -n "$h" ] && [ -e "$D/bench.txt" ] && echo "harness=$h" >> "$D/bench.txt"
	if [ -s $TIMERS ]; then	# which periodic jobs were kept out of this run
		[ -e "$D/bench.txt" ] && echo "timers_paused=$(tr '\n' ' ' < $TIMERS)" >> "$D/bench.txt"
		cp $TIMERS "$D/timers-paused"
	fi
	for x in "$HOME/.local/share/xorg/Xorg.0.log" /var/log/Xorg.0.log; do	# the bare X's log
		[ "$x" -nt "$D/cmd" ] && cp "$x" "$D/" && break
	done
	{ echo "rc $rc wall $(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.2f", b - a }') s"
	  # the client's CPU time, and games/bench's summary (bench: key=value)
	  grep -h -a -E '^[0-9.]+ (client|bench): ' "$D/out.log" | cut -d' ' -f2-
	  # fps per second after loading: the game's load time from games/bench, else 12 s
	  skip=$(awk -F= '$1 == "load" && $2 != "" { s = int($2) + 1 } $1 == "hud_skip" && !s { s = $2 }
		END { print s ? s : 12 }' "$D/bench.txt" 2>/dev/null || echo 12)
	  [ -s "$D/hud/fps" ] && awk -v s="$skip" 'NR>s{print int($1)}' "$D/hud/fps" | sort -n |
		awk '{a[NR]=$1} END{if (NR) printf "hud fps: median %d p10 %d min %d (%d s)\n", a[int(NR/2)+1], a[int(NR/10)+1], a[1], NR}'
	  awk 'NR==1{m=$2} $2<m{m=$2} END{if (NR) printf "min MemAvailable %d kB\n", m}' "$D/samples" 2>/dev/null
	  [ -s "$D/timers-paused" ] && echo "timers paused: $(tr '\n' ' ' < "$D/timers-paused")"
	  [ -n "$h" ] && echo "harness: $h"
	  # the stall recorder's own cost (its trailer)
	  for f in "$D/stalls.rec" "$D/stallrec-nowrite.rec"; do
		[ -s "$f" ] && awk '$1 == "#" && $2 == "end" { for (i = 3; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
			s = v["t"] / 1e6; c = (v["self_user_us"] + v["self_sys_us"]) / 1e6
			printf "stallrec: %d samples, own CPU %.2f s (%.2f%% of a thread), late %d, largest gap %d ms\n",
				v["samples"], c, s ? 100 * c / s : 0, v["late"], v["maxgap_us"] / 1000 }' "$f"
	  done
	  # ps3-waker: what it did and its cost; the probe: each CPU's late timers
	  [ -s "$D/waker.txt" ] && awk '$1 == "sleep" || $1 == "kick" { l = l (l ? "; " : "") $0 }
		$1 == "#" && $2 == "end" { for (i = 3; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
			c = (v["self_user_us"] + v["self_sys_us"]) / 1e6 }
		END { printf "waker: %s; own CPU %.2f s (%.2f%% of a thread)\n", l, c, v["t"] ? 100 * c / v["t"] : 0 }' "$D/waker.txt"
	  [ -s "$D/probe.txt" ] && awk '$1 == "cpu" { print "probe: " $0 }' "$D/probe.txt"
	  # perf's own CPU over the window (client.sh's perf_cpu=) and its file
	  [ -e "$D/perf.targets" ] && awk -F= -v t="$(cat "$D/perf.track" 2>/dev/null)" '$1 == "perf_win" { w = $2 } $1 == "perf_cpu" { c = $2 }
		END { printf "perf: window %s, perf itself %s s of CPU in it, load mmaps tracked: %s\n", w ? w : "never enabled",
			c ? c : "?", t ? t : "?" }' "$D/bench.txt"
	} > "$D/result"
	[ -e "$D/THERMAL" ] && echo "!! THERMAL ALERT: the job was stopped. Let the PS3 cool down." >&2
	mkdir -p $OUT; mv "$D" $OUT/; D=$OUT/${D##*/}
	if [ -n "$PD" ]; then	# the profile from the disk into the run directory, readable by us
		sudo chown -R "$(id -u):$(id -g)" "$PD"
		mv "$PD"/* "$D/" 2>/dev/null; rmdir "$PD"; PD=
	fi
	sed 's/^/>> /' "$D/result"; echo ">> $D"
}

case ${1:-} in
quiet) quiet ;;
snap) [ -n "${D:-}" ] || die "snap: set D to a run directory"; snap "${2:?label}" ;;
run|x)
	mode=$1; label=${2:?label}; shift 2; [ "${1:-}" = -- ] && shift
	[ $# -gt 0 ] || die "no command"
	quiet
	if [ "$mode" = x ]; then
		# xinit runs a client only if its path starts with / (else it starts xterm): /bin/bash,
		# which also reports the client's own CPU time
		set -- xinit /bin/bash -c 'TIMEFORMAT="client: real %R s, user %U s, sys %S s"; time "$@"' \
			client "$@" -- :0 vt1 -nolisten tcp
	fi
	run_job "$label" "$@"
	;;
desktop)
	vswap_on
	resume_timers
	rm -f $LOCK
	sudo systemctl start getty@tty1
	echo '>> desktop started, bench session over'
	;;
*) sed -n '2,/^[^#]/s/^# \{0,1\}//p' "$0"; exit 2 ;;
esac
exit
}
