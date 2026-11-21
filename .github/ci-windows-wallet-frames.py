"""Observe the normal shutdown of the published Qt wallet on a disposable runner."""

import importlib.util
import os
from pathlib import Path
import re
import subprocess
from unittest.mock import patch

if os.name != "nt" or os.environ.get("GITHUB_ACTIONS") != "true":
    raise SystemExit("Requires an ephemeral Windows Actions runner")

controls = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("wallet_acceptance", controls / "ci-windows-smoke.py")
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)
qt = Path("distribution/bin/sugarchain-qt.exe").resolve()
cli = qt.with_name("sugarchain-cli.exe")
workspace = Path(os.environ["RUNNER_TEMP"]) / "wallet-source-observation"
gdb = Path(os.environ["WALLET_SOURCE_GDB"])
if not all(p.is_file() for p in (qt, cli, gdb)):
    raise SystemExit("The verified release executables and native tools are required")

popen = subprocess.Popen


def observe_wallet(argv, *args, **kwargs):
    if isinstance(argv, list) and argv and Path(argv[0]) == qt and "-debug=qt" in argv:
        argv = [str(gdb), "--nx", "--batch", "--return-child-result",
                "-iex", "set auto-load off", "-x", str(controls / "ci-windows-normal-exit.gdb"),
                "--args", *argv]
    return popen(argv, *args, **kwargs)

try:
    with patch.object(subprocess, "Popen", observe_wallet):
        result = smoke.check_node(qt, cli, workspace, "main", gui=True)
    print(f"Normal mainnet wallet returned successfully: {result['wallet']}")
finally:
    log = workspace / "process.log"
    if log.is_file():
        in_stack = False
        for line in log.read_text(errors="replace").splitlines():
            if line.startswith("Wallet frame names begin"):
                in_stack = True
            elif line.startswith("Wallet frame names end"):
                in_stack = False
            elif in_stack and re.match(r"^#\d+\s", line):
                print(line[:400])
            elif line.startswith(("Wallet inferior exit code:", "Wallet observation stopped before process exit")):
                print(line)
            elif any(marker in line.lower() for marker in ("error in sourced command file", "error creating process", "not in executable format", "cannot execute", "during startup program exited")):
                print("Native tool: " + line[:240])
