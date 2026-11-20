"""Report the isolated Windows Qt runtime and optionally exercise an installed QPA module."""
import importlib.util
import os
from pathlib import Path
import sys


def installed_platform_modules():
    roots = [Path("build/vcpkg_installed"), Path("vcpkg_installed")]
    return sorted({
        p.resolve() for root in roots if root.exists()
        for p in root.glob("x64-windows/**/platforms/qwindows.dll")
        if "debug" not in (part.lower() for part in p.parts)
    })


def report_files(datadir, bindir):
    for name in ("sugarchain-qt.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
                 "Qt6Network.dll", "platforms/qwindows.dll", "qt.conf"):
        item = bindir / name
        print("Build runtime", name, item.stat().st_size if item.is_file() else "absent")
    for name in (".cookie", "process.log", "debug.log"):
        file = datadir / name
        print("GUI startup file", name, file.is_file())
    log = datadir / "process.log"
    if log.is_file():
        markers = ("qwindows", "platform plugin", "qt.qpa", "cannot load library", "could not load")
        rows = [line for line in log.read_text(errors="replace").splitlines()
                if any(token in line.lower() for token in markers)]
        for line in rows[-16:]:
            print("GUI runtime:", line[:300])
    for plugin in installed_platform_modules():
        print("Installed Windows platform module:", plugin.relative_to(Path.cwd()))


if __name__ == "__main__":
    root = Path(os.environ["RUNNER_TEMP"])
    binaries = Path("build/bin/Release").resolve()
    if sys.argv[1:] == ["--use-installed-plugin"]:
        candidates = installed_platform_modules()
        if not candidates:
            raise SystemExit("No release Windows QPA module found in the vcpkg installation")
        os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(candidates[0].parent)
        spec = importlib.util.spec_from_file_location("smoke", ".github/ci-windows-smoke.py")
        smoke = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(smoke)
        target = root / "en-gui-explicit-platform"
        try:
            print(smoke.check_node(binaries / "sugarchain-qt.exe", binaries / "sugarchain-cli.exe",
                                   target, "main", gui=True))
        finally:
            report_files(target, binaries)
    else:
        report_files(root / "en-confirmed-wallets" / "gui-main", binaries)
