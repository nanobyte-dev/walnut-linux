# bench/: the game benchmark harness that runs on the PS3 (`make bench-push`): run.sh, client.sh, the recipes per game, the analysis scripts, the result matrices in results/, and the played-session tools.
Results come back with `make bench-pull`; `python3 bench/selftest.py` checks the harness without the PS3.
Touch it to add a game's recipe or change how runs are measured (docs/games/SUITE.md).
