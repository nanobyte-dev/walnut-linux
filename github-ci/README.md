# github-ci/: what GitHub Actions runs: building one package, publishing it to testing or stable, the release image, and gen-workflows.py, which writes .github/workflows/pkg-<name>.yml.
Run `python3 github-ci/gen-workflows.py` after adding, moving or removing a package; licensed packages never get a workflow.
Touch it when CI's build or publish steps change (docs/release/ALPHA.md M6).
