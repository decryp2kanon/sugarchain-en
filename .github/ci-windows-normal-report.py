"""Run the existing wallet acceptance script with standard process error reporting."""

import ctypes
import os
from pathlib import Path
import runpy
import sys

if os.name != "nt" or os.environ.get("GITHUB_ACTIONS") != "true":
    raise SystemExit("Requires an ephemeral Windows Actions runner")

driver = Path(sys.argv[1]).resolve()
if driver.parent != Path(__file__).resolve().parent or driver.name not in {
    "ci-windows-smoke.py", "ci-windows-installer.py",
}:
    raise SystemExit("Expected a wallet acceptance script")

kernel = ctypes.WinDLL("kernel32")
previous = kernel.GetErrorMode()
# SEM_NOGPFAULTERRORBOX suppresses Windows Error Reporting for child processes.
no_error_reporting = 0x0002
try:
    if previous & no_error_reporting:
        kernel.SetErrorMode(previous & ~no_error_reporting)
    current = kernel.GetErrorMode()
    print(f"Windows process error mode: inherited {previous:#x}, wallet parent {current:#x}", flush=True)
    if current & no_error_reporting:
        raise RuntimeError("Standard Windows error reporting is unavailable")
    sys.argv = sys.argv[1:]
    runpy.run_path(str(driver), run_name="__main__")
finally:
    kernel.SetErrorMode(previous)
