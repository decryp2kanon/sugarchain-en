"""Run the published Qt test on the Windows desktop and retain its observed output."""
from hashlib import file_digest
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

work = Path(os.environ["RUNNER_TEMP"]) / "sugarchain-qt-artifact-result"
work.mkdir(exist_ok=True)
app = Path("bin/test_bitcoin-qt.exe").resolve(strict=True)
spec = importlib.util.spec_from_file_location("native_smoke", Path(__file__).with_name("ci-windows-smoke.py"))
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)
env = dict(os.environ, QT_QPA_PLATFORM="windows", QT_FORCE_STDERR_LOGGING="1", QT_LOGGING_TO_CONSOLE="1")
env["DIR_UNIT_TEST_DATA"] = str(Path("unit_test_data").resolve())
with app.open("rb") as f:
    digest = file_digest(f, "sha256").hexdigest()
with (work / "qt-unit.log").open("wb") as out:
    with subprocess.Popen([str(app)], stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT, env=env) as proc:
        code = proc.wait(timeout=600)
        module = ""
        if code:
            for _ in range(5):
                module = smoke.windows_fault_module(proc)
                if module:
                    break
                time.sleep(1)
log = (work / "qt-unit.log").read_bytes()
summary = dict(code=code, pid=proc.pid, sha256=digest, platform="windows", module=module, log_bytes=len(log))
print(json.dumps(summary), flush=True)
(work / "qt-unit.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
for line in log.decode("utf-8", "replace").splitlines():
    if line.lstrip().startswith(("FAIL!", "QFATAL", "Totals:", "Failed tests:", "All tests passed.", "terminate called", "ASSERT", "Error:")):
        print(line[:400], flush=True)
raise SystemExit(int(code != 0))
