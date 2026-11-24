from __future__ import annotations

from contextlib import contextmanager
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import time


ROOT = Path.cwd()
REPORT = ROOT / "evidence"
DATA = Path.home() / "Library/Application Support/Sugarchain"
APP = Path("/Applications/Sugarchain En.app")
GUI = APP / "Contents/MacOS/Sugarchain-Qt"
CLI_DIR = next((ROOT / "packages").glob("*-cli/bin"))
EVIDENCE = {}


def run(command, timeout=15):
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout)


def expect(condition, message):
    if not condition:
        raise ValueError(message)


def desktop_processes():
    lines = run(["ps", "-axo", "pid=,comm="]).stdout.splitlines()
    parts = [line.strip().split(maxsplit=1) for line in lines]
    return [int(pid) for pid, executable in parts if executable == str(GUI)]


def screenshot(label, timeout=14):
    helper = Path(os.environ["RUNNER_TEMP"]) / "macos-window-id"
    end = time.monotonic() + timeout
    last = "No app window found"
    while time.monotonic() < end:
        windows = run([helper, APP.stem]).stdout.strip().splitlines()
        for line in windows:
            window, _width, _height = line.split()
            png = REPORT / f"{label}.png"
            capture = run(["screencapture", "-x", "-l", window, str(png)])
            if capture.returncode == 0 and png.exists() and png.stat().st_size > 3000:
                EVIDENCE[label] = {"captured": True, "window": line, "bytes": png.stat().st_size}
                return
            last = capture.stderr.strip() or "Screenshot did not contain a usable image"
        time.sleep(1)
    EVIDENCE[label] = {"captured": False, "detail": last}


def screenshot_first_run():
    open_process = subprocess.Popen(
        ["open", "-n", "-W", "-a", APP, "--args", "-regtest", "-listen=0",
         "-connect=0", "-dnsseed=0", "-discover=0", "-networkactive=0"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        screenshot("first-run")
    finally:
        for pid in desktop_processes():
            os.kill(pid, signal.SIGTERM)
        try:
            open_process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            for pid in desktop_processes():
                os.kill(pid, signal.SIGKILL)
            open_process.kill()
            open_process.wait(timeout=5)


@contextmanager
def node(label, binary, cli, chain, *, gui=False, legacy=False):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    common = [f"-rpcport={port}"]
    if chain == "regtest":
        common.append("-regtest")
    options = ["-server=1", "-listen=0", "-connect=0", "-dnsseed=0", "-discover=0"]
    if not legacy:
        options += ["-networkactive=0", "-printtoconsole=0"]
    if gui:
        options += ["-splash=0", "-debug=qt"]
    command = ["open", "-n", "-W", "-a", APP, "--args"] if gui else [binary]
    stage = {"chain": chain, "gui_via_launch_services": gui}
    EVIDENCE[label] = stage

    def rpc(*args, check=True):
        version_args = ["-rpcclienttimeout=4"] if not legacy else []
        result = run([cli, *common, *version_args, *map(str, args)], timeout=8)
        if result.returncode and check:
            raise ValueError(f"{args[0]} failed: {result.stderr.strip()[:400]}")
        return result

    with (REPORT / f"{label}-process.log").open("wb") as log:
        start = time.monotonic()
        process = subprocess.Popen([*command, *common, *options], stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                chaininfo = rpc("getblockchaininfo", check=False)
                if chaininfo.returncode == 0:
                    stage["startup_seconds"] = round(time.monotonic() - start, 2)
                    stage["blocks"] = json.loads(chaininfo.stdout)["blocks"]
                    break
                if process.poll() is not None:
                    raise ValueError(f"{label} exited before RPC readiness ({process.returncode})")
                time.sleep(0.35)
            else:
                raise ValueError(f"{label} did not reach RPC readiness")
            yield rpc, stage
        finally:
            if process.poll() is None:
                start = time.monotonic()
                stopped = rpc("stop", check=False)
                stage["stop_rpc"] = stopped.returncode
                try:
                    process.wait(timeout=70)
                except subprocess.TimeoutExpired:
                    if gui:
                        for pid in desktop_processes():
                            os.kill(pid, signal.SIGTERM)
                    process.kill()
                    process.wait(timeout=8)
                    stage["shutdown_timeout"] = True
                stage["shutdown_seconds"] = round(time.monotonic() - start, 2)
            stage["exit_code"] = process.returncode
            if stage.get("shutdown_timeout") or stage["exit_code"] != 0:
                stage["shutdown_problem"] = True


def default_directory_roundtrip():
    data = DATA / "regtest"
    data.mkdir(parents=True, exist_ok=True)
    (data / "wallets").mkdir(exist_ok=True)
    daemon, cli = CLI_DIR / "sugarchaind", CLI_DIR / "sugarchain-cli"
    name = "저축 지갑"
    with node("default-daemon", daemon, cli, "regtest") as (rpc, stage):
        expect((data / "debug.log").is_file(), "Daemon did not use the standard macOS directory")
        created = rpc("-named", "createwallet", f"wallet_name={name}", "load_on_startup=true")
        expect(json.loads(created.stdout)["name"] == name, "Wallet name did not round-trip")
        address = rpc(f"-rpcwallet={name}", "getnewaddress", "", "bech32").stdout.strip()
        expect(address.startswith("bcrt1q"), "Unexpected regtest address format")
        tip = json.loads(rpc("generatetoaddress", 1, address).stdout)[0]
        stage["standard_data_path"] = True
        stage["created_korean_wallet"] = True
        stage["regtest_tip"] = tip
    for label in ("default-gui", "restarted-gui"):
        with node(label, GUI, cli, "regtest", gui=True) as (rpc, stage):
            expect(rpc("getbestblockhash").stdout.strip() == tip, "GUI tip differs from daemon tip")
            expect(name in json.loads(rpc("listwallets").stdout), "GUI did not open the daemon wallet")
            owned = json.loads(rpc(f"-rpcwallet={name}", "getaddressinfo", address).stdout)["ismine"]
            expect(owned, "GUI wallet no longer owns the daemon address")
            expect((data / "wallets" / name / "wallet.dat").is_file(), "Wallet file not at the shared path")
            stage.update({"matched_daemon_tip": True, "reopened_korean_wallet": True, "standard_data_path": True})
            if label == "default-gui":
                screenshot("wallet-gui")
    with node("restarted-daemon", daemon, cli, "regtest") as (rpc, stage):
        expect(rpc("getbestblockhash").stdout.strip() == tip, "Daemon tip differed after GUI shutdown")
        expect(name in json.loads(rpc("listwallets").stdout), "Daemon failed to reopen wallet")
        stage["roundtrip_passed"] = True


def legacy_wallet():
    files = list((ROOT / "packages/legacy").rglob("sugarchaind"))
    if not files:
        EVIDENCE["legacy_upgrade"] = {"ran": False, "reason": "Legacy release asset is x86_64 only"}
        return
    old = files[0]
    new, cli = CLI_DIR / "sugarchaind", CLI_DIR / "sugarchain-cli"
    with node("visioneye-daemon", old, old.with_name("sugarchain-cli"), "main", legacy=True) as (rpc, stage):
        address = rpc("getnewaddress").stdout.strip()
        expect(address.startswith("S"), "Legacy address format differs")
        stage["created_legacy_wallet"] = True
    expect((DATA / "wallet.dat").is_file() or (DATA / "wallets/wallet.dat").is_file(),
           "Legacy wallet did not appear in the default data directory")
    with node("visioneye-upgrade", new, cli, "main") as (rpc, stage):
        migrate = rpc("migratewallet", "", check=False)
        stage["migration_status"] = migrate.returncode
        if migrate.returncode != 0:
            raise ValueError(f"migratewallet: {migrate.stderr.strip()[:400]}")
        result = json.loads(migrate.stdout)
        name = result["wallet_name"]
        owned = json.loads(rpc(f"-rpcwallet={name}", "getaddressinfo", address).stdout)["ismine"]
        expect(owned, "Migration lost ownership of the Visioneye address")
        stage["migration_preserved_address"] = True
        stage["migration_backup_exists"] = Path(result["backup_path"]).is_file()


def main():
    expect(os.environ.get("GITHUB_ACTIONS") == "true", "Run on an ephemeral GitHub Actions host only")
    expect(not DATA.exists(), "Expected no pre-existing Sugarchain directory on the QA runner")
    for label, check in (("first_run_probe", screenshot_first_run),
                         ("default_directory_probe", default_directory_roundtrip),
                         ("legacy_wallet_probe", legacy_wallet)):
        try:
            check()
        except Exception as exc:
            EVIDENCE[label] = {"passed": False, "detail": str(exc)}
        else:
            EVIDENCE[label] = {"passed": True}
    for name in ("debug.log", "regtest/debug.log"):
        p = DATA / name
        if p.is_file():
            lines = p.read_text(errors="replace").splitlines()
            selected = [line for line in lines if any(term in line.casefold() for term in (
                "using data directory", "fatal", "error", "warning", "shutdown", "script verification",
                "platform", "wallet settings", "fast startup", "chainstate", "new best"))]
            (REPORT / name.replace("/", "-")).write_text("\n".join(selected[-120:]) + "\n")
    (REPORT / "results.json").write_text(json.dumps(EVIDENCE, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps(EVIDENCE, indent=2, ensure_ascii=False))
    if not all(EVIDENCE[k]["passed"] for k in (
        "first_run_probe", "default_directory_probe", "legacy_wallet_probe"
    )):
        raise SystemExit("One or more macOS audit checks require attention.")


if __name__ == "__main__":
    main()
