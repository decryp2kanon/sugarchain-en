#!/usr/bin/env python3
# Copyright (c) 2026 The Sugarchain En developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

"""Exercise native production binaries with isolated, offline data directories."""

import argparse
import base64
import contextlib
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request


def runtime_environment(datadir, platform=None):
    platform = os.name if platform is None else platform
    env = os.environ.copy()
    for kind in ("CONFIG", "DATA", "CACHE"):
        env[f"XDG_{kind}_HOME"] = str(datadir / f"xdg-{kind.lower()}")
    # The minimal plugin cannot load application fonts. Windows runners
    # provide the native desktop; Linux's dynamic Qt build has offscreen.
    env["QT_QPA_PLATFORM"] = "windows" if platform == "nt" else "offscreen"
    if platform != "nt":
        env["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=/nonexistent-sugarchain-en-ci-bus"
    return env


def check_version(binary, program, datadir, env):
    command = [str(binary)]
    # The daemon reads configuration before handling -version. CI guards the
    # default data directory with a file, so keep this probe private too.
    if program == "sugarchaind":
        command.append(f"-datadir={datadir}")
    command.append("--version" if program == "sugarchain" else "-version")
    result = subprocess.run(command, capture_output=True, text=True, timeout=30, env=env)
    if result.returncode != 0:
        raise RuntimeError(f"{program} version probe exited {result.returncode}:\n{result.stdout}{result.stderr}")
    assert "Sugarchain En" in result.stdout, (program, result.stdout, result.stderr)


class Node:
    def __init__(self, binary, datadir, chain, gui=False):
        self.datadir = datadir
        self.chain = chain
        self.wallet = None
        datadir.mkdir(parents=True)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            self.port = listener.getsockname()[1]
        command = [
            str(binary), f"-datadir={datadir}", f"-rpcport={self.port}",
            "-server=1", "-listen=0", "-connect=0", "-dnsseed=0", "-discover=0",
            "-networkactive=0", "-maxconnections=0", "-keypool=10",
            "-fallbackfee=0.0002", "-printtoconsole=0",
        ]
        if chain == "regtest":
            command.append("-regtest")
        if gui:
            command += ["-splash=0", "-min"]
        self.cookie = datadir / ("regtest/.cookie" if chain == "regtest" else ".cookie")
        self.log = (datadir / "process.log").open("wb")
        self.process = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT, env=runtime_environment(datadir))

    def rpc(self, method, *params):
        auth = base64.b64encode(self.cookie.read_bytes().strip()).decode()
        wallet_path = f"/wallet/{urllib.parse.quote(self.wallet, safe='')}" if self.wallet else "/"
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{wallet_path}",
            json.dumps({"jsonrpc": "2.0", "id": "smoke", "method": method, "params": params}).encode(),
            {"Authorization": f"Basic {auth}", "Content-Type": "application/json"},
        )
        try:
            response = urllib.request.urlopen(request, timeout=30)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            result = json.load(response)
        if result.get("error"):
            raise RuntimeError(result["error"])
        return result["result"]

    def wait(self):
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"Node exited before RPC startup; see {self.datadir / 'process.log'}")
            try:
                return self.rpc("getblockchaininfo")
            except (OSError, RuntimeError):
                time.sleep(0.1)
        raise RuntimeError(f"Node startup timed out; see {self.datadir / 'process.log'}")

    def close(self):
        try:
            if self.process.poll() is None:
                self.wallet = None
                with contextlib.suppress(OSError, RuntimeError):
                    self.rpc("stop")
                try:
                    self.process.wait(timeout=45)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    self.process.wait(timeout=15)
                    raise
            assert self.process.returncode == 0, self.process.returncode
        finally:
            self.log.close()


def check_node(binary, cli, datadir, chain, gui=False):
    node = Node(binary, datadir, chain, gui)
    try:
        info = node.wait()
        assert info["chain"] == chain and info["blocks"] == 0, info
        expected_genesis = {
            "main": "7d5eaec2dbb75f99feadfa524c78b7cabc1d8c8204f79d4f3a83381b811b0adc",
            "regtest": "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206",
        }
        assert node.rpc("getblockhash", 0) == expected_genesis[chain]
        network = node.rpc("getnetworkinfo")
        assert network["networkactive"] is False and network["connections"] == 0
        command = [str(cli), f"-datadir={datadir}", f"-rpcport={node.port}"]
        if chain == "regtest":
            command.append("-regtest")
        command.append("getblockcount")
        assert subprocess.check_output(command, text=True).strip() == "0"

        node.rpc("createwallet", "smoke", False, False, "", False, True, False)
        node.wallet = "smoke"
        address = node.rpc("getnewaddress", "", "bech32")
        assert address.startswith("sugar1q" if chain == "main" else "bcrt1q"), address
        assert node.rpc("getaddressinfo", address)["ismine"]
        legacy = node.rpc("getnewaddress", "", "legacy")
        if chain == "main":
            assert legacy.startswith("S"), legacy
        message = "Sugarchain En native Windows wallet smoke"
        signature = node.rpc("signmessage", legacy, message)
        assert node.rpc("verifymessage", legacy, signature, message)
        if chain == "main":
            try:
                node.rpc("getnewaddress", "", "bech32m")
            except RuntimeError as error:
                assert "not supported on this network" in str(error), error
            else:
                raise AssertionError("Inactive mainnet Taproot unexpectedly generated an address")
        else:
            blocks = node.rpc("generatetoaddress", 101, address)
            assert len(blocks) == 101
            txid = node.rpc("sendtoaddress", node.rpc("getnewaddress"), 1)
            node.rpc("generatetoaddress", 1, address)
            assert node.rpc("gettransaction", txid)["confirmations"] == 1
            backup = datadir / "fresh-wallet-backup.sqlite"
            node.rpc("backupwallet", str(backup))
            assert backup.is_file() and backup.stat().st_size > 0
            node.wallet = None
            node.rpc("restorewallet", "restored", str(backup), False)
            node.wallet = "restored"
            assert node.rpc("getaddressinfo", address)["ismine"]
        return {"chain": chain, "gui": gui, "height": node.rpc("getblockcount"), "wallet": "passed"}
    finally:
        node.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bindir", required=True, type=Path)
    parser.add_argument("--workdir", "--work-dir", dest="workdir", type=Path)
    args = parser.parse_args()
    bindir = args.bindir.resolve()
    workdir = args.workdir.resolve() if args.workdir else Path(tempfile.mkdtemp(prefix="sugarchain-en-smoke-"))
    workdir.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""
    programs = ["sugarchain", "sugarchaind", "sugarchain-cli", "sugarchain-qt", "sugarchain-wallet", "sugarchain-tx", "sugarchain-util"]
    binaries = {}
    version_datadir = workdir / "version-data"
    version_datadir.mkdir()
    version_env = runtime_environment(workdir / "version-env")
    for program in programs:
        candidates = [bindir / f"{program}{suffix}", bindir / "daemon" / f"{program}{suffix}"]
        binary = next((path for path in candidates if path.is_file()), candidates[0])
        assert binary.is_file(), binary
        binaries[program] = binary
        check_version(binary, program, version_datadir, version_env)
    daemon = binaries["sugarchaind"]
    cli = binaries["sugarchain-cli"]
    results = [
        check_node(daemon, cli, workdir / "main", "main"),
        check_node(daemon, cli, workdir / "regtest", "regtest"),
        check_node(binaries["sugarchain-qt"], cli, workdir / "gui-main", "main", gui=True),
    ]
    for chain in ("test", "testnet4", "signet"):
        datadir = workdir / f"unsupported-{chain}"
        datadir.mkdir()
        result = subprocess.run([str(daemon), f"-datadir={datadir}", f"-chain={chain}"], capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and "not supported" in result.stderr, result
        assert not list(datadir.iterdir()), f"Rejected {chain} created persistent data"
    report = {"binaries": len(programs), "runtime": results, "unsupported_chains_rejected": 3, "status": "passed"}
    (workdir / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
