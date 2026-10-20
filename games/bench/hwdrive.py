#!/usr/bin/python3 -I
# hwdrive.py MISSION [hwengine args...]: starts hwengine as the Hedgewars Qt frontend does
# (--internal --port N, the engine connects to 127.0.0.1:N) and plays the frontend's part of the IPC
# for a training mission, so the game runs without the Qt launcher (no Qt libraries, hedgewars-engine
# package only). MISSION is a file of Data/Missions/Training (no .lua), e.g. Basic_Training_-_Movement.
# The protocol is QTfrontend/game.cpp: messages are one length byte and the payload. The engine asks
# '?' (answered '!') and 'C' (config); the config is "TL" (training), the mission team
# (esetmissteam, eaddmisshh x4), a seed and "escript Missions/Training/<MISSION>.lua"
# (HWGame::SendTrainingConfig). Ends when the engine exits or sends 'q'/'Q'/'E'. Exit code: the engine's.
# 'q' means the game ended by itself (the mission's EndGame); on the PS3 the Movement mission does that after
# about 45 s without any input (the final crate's deletion calls victory(), probably it falls into the water: docs/games/smoke-tested.md), on amd64 it runs on for 250 s.
# Env: HW_DATA (Data dir, default /usr/share/games/hedgewars/Data), HW_USER (default ~/.hedgewars),
# HW_FOLDER (Training, or Scenario ...), HW_TEAM (default "Cosmic Avenger"), HW_HOGS (4).
import os, socket, subprocess, sys, time

if len(sys.argv) < 2:
    sys.exit("usage: hwdrive.py MISSION [hwengine args]")
mission, extra = sys.argv[1], sys.argv[2:]
data = os.environ.get("HW_DATA", "/usr/share/games/hedgewars/Data")
user = os.environ.get("HW_USER", os.path.expanduser("~/.hedgewars"))
folder = os.environ.get("HW_FOLDER", "Training")
team = os.environ.get("HW_TEAM", "Cosmic Avenger")
hogs = int(os.environ.get("HW_HOGS", "4"))
engine = os.environ.get("HW_ENGINE", "/usr/lib/hedgewars/bin/hwengine")

def msg(s):
    b = s.encode()
    assert len(b) < 256
    return bytes([len(b)]) + b

def config():
    out = [msg("TL")]
    for line in [f"esetmissteam 00000000000000000000000000000000 4421856 {team}",
                 "egrave Statue", "efort Plane", "evoicepack Default", "eflag hedgewars"]:
        out.append(msg(line))
    for i in range(hogs):
        out.append(msg(f"eaddmisshh 0 100 Hog {i + 1}"))
        out.append(msg("ehat NoHat"))
    out.append(msg("eseed hwdrive-" + str(int(time.time()))))
    out.append(msg(f"escript Missions/{folder}/{mission}.lua"))
    return b"".join(out)

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0))
srv.listen(1)
port = srv.getsockname()[1]
cmd = [engine, "--internal", "--port", str(port), "--prefix", data, "--user-prefix", user] + extra
print("hwdrive:", " ".join(cmd), flush=True)
os.makedirs(user, exist_ok=True)
proc = subprocess.Popen(cmd)
srv.settimeout(60)
try:
    conn, _ = srv.accept()
except socket.timeout:
    proc.kill()
    sys.exit("hwdrive: the engine never connected")
conn.settimeout(1.0)
buf = b""
done = False
while not done and proc.poll() is None:
    try:
        chunk = conn.recv(4096)
    except socket.timeout:
        continue
    if not chunk:
        break
    buf += chunk
    while buf and len(buf) > buf[0]:
        n = buf[0]
        m, buf = buf[1:1 + n], buf[1 + n:]
        t = m[:1]
        if t == b"?":
            conn.sendall(msg("!"))
        elif t == b"C":
            conn.sendall(config())
            print("hwdrive: sent the training config for", mission, flush=True)
        elif t in (b"q", b"Q", b"H"):
            # 'q' is the engine's end of game (uGearsHandlersMess gtATFinishGame: the mission called
            # EndGame, here victory() of the Movement mission), not a quit request: the driver adds nothing
            print("hwdrive: engine says", t.decode(), "(the game is over: the mission ended itself)", flush=True)
            done = True
        elif t == b"E":
            print("hwdrive: engine error:", m[1:].decode(errors="replace"), flush=True)
            done = True
if proc.poll() is None:
    proc.terminate()
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        proc.kill()
sys.exit(proc.returncode or 0)
