# apt-repo/: the apt repositories at walnut.nanobyte.dev, from the client side: walnut-repo.py (builds and signs a suite's next state, no database), its tests, the keyrings, and licensed-publish.sh (`make publish`) for the password-protected licensed suite.
CI publishes testing and stable with these (github-ci/publish.sh); only licensed packages are published from this PC.
Touch it when the suites, their keys or their retention change (docs/PACKAGING.md "The repository").
