# En Linux Qt CI

The [En Linux Qt workflow](../.github/workflows/en-linux-qt.yml) builds the
Linux applications and runs the complete Qt test executable on Ubuntu 24.04.
It runs on branch pushes, pull requests targeting `en`, and manual dispatches.
A pull request update cancels an older baseline run for the same source branch.
Manual dispatch becomes available after the workflow is on the default branch.

The job explicitly enables the GUI, wallet, IPC, Qt tests, ZMQ, and USDT. It
builds the daemon, CLI, IPC wrapper, IPC node, both GUI variants, wallet tool,
and Qt test executable. CMake target names retain their internal `bitcoin`
identifiers; the installed application names use Sugarchain branding.

The test step selects `^test_bitcoin-qt$` with `--no-tests=error`. It runs the
whole executable, including URI, RPC, wallet, and payment IPC tests, without
skipping individual Qt methods. It uses Qt's headless `minimal` platform and
isolated XDG directories. Configuration and test diagnostics are uploaded on
failure and retained for seven days.

For a local run, install the packages and use the configure, build, and CTest
commands from the workflow. Run in a fresh build directory with disposable XDG
directories; do not point tests at an operational wallet or node data directory.

The inherited [upstream CI matrix](../.github/workflows/ci.yml) is preserved.
The En Linux Qt job does not establish that the full core or functional suites
pass, that wallet address encodings are compatible with legacy Sugarchain, or
that Windows and macOS releases are ready. Those checks remain separate work.
