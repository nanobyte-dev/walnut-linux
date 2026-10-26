# bench/: the game benchmark harness that runs on the PS3 (`make bench-push`): run.sh, client.sh (the recipes are in each game's gamedb/games/<id>/benchmark/: games.sh finds them), the analysis scripts, the result matrices in results/, and the played-session tools.
Results come back with `make bench-pull`; `python3 bench/selftest.py` checks the harness without the PS3.
Touch it to change how runs are measured; a game's recipe goes in its bundle (docs/games/SUITE.md).
