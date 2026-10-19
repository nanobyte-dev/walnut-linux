# Hedgewars 1.0.3 (our hedgewars-engine and hedgewars-data, 1.0.3-5+ps3.N): smoke test, not
# benchmarked. Runs the engine (hwengine, Free Pascal, SDL2 with its OpenGL 1.x/2.x renderer) WITHOUT
# the Qt frontend: games/bench/hwdrive.py starts hwengine the way the frontend does (--internal
# --port N) and plays the frontend's end of the IPC for a training mission (QTfrontend/game.cpp,
# SendTrainingConfig), straight into "Basic Training - Movement" at 1920x1080 fullscreen. The player
# controls a hedgehog (Enter jumps, arrows walk, Tab switches, Space fires); the mission teaches the
# moves step by step. Variants: bazooka, rope (other training missions), lowq (--low-quality), nosound,
# window. Needs only hedgewars-engine (+ hedgewars-data, 160 MB), no Qt5: `apt install
# hedgewars-engine`. The engine starts with the FPU exceptions masked (hedgewars/ patch 0001: without
# it ppc64 dies with EInvalidOp before the first step). The user data goes to ~/.hedgewars.
COMM=hwengine
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.hedgewars/settings.ini")
HW_MISSION=Basic_Training_-_Movement
HW_ARGS=(--fullscreen --fullscreen-width 1920 --fullscreen-height 1080 --width 1920 --height 1080
	--locale en.txt --showfps 1 --nick smoke)
hw_cmd() { CMD=(python3 -I "$here/hwdrive.py" "$HW_MISSION" "${HW_ARGS[@]}"); }
hw_cmd
setup() { [ -x /usr/lib/hedgewars/bin/hwengine ] || echo "WARNING: no hwengine: apt install hedgewars-engine"; }
variant_bazooka() { HW_MISSION=Basic_Training_-_Bazooka; hw_cmd; }
variant_rope() { HW_MISSION=Basic_Training_-_Rope; hw_cmd; }
variant_lowq() { HW_ARGS+=(--low-quality); hw_cmd; }
variant_nosound() { HW_ARGS+=(--nosound --nomusic); hw_cmd; }
variant_window() { HW_ARGS=(--width 1280 --height 720 --locale en.txt --showfps 1 --nick smoke); hw_cmd; }
