"""Compile the wallet's payment IPC and its original Qt Test assertions with the Windows SDK."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path.cwd()
temp = Path(os.environ["RUNNER_TEMP"])
qt = temp / "payment-sdk/6.8.3/msvc2022_64"
mode, latency = sys.argv[1:]
assert mode in ("write", "duplex") and latency in ("normal", "delayed")
area = temp / f"wallet-ipc-{mode}-{latency}"
tests = area / "src/qt/test"
tests.mkdir(parents=True)
files = ("src/qt/paymentipc.cpp", "src/qt/paymentipc.h", "src/qt/test/paymentipctests.cpp", "src/qt/test/paymentipctests.h")
for name in files:
    shutil.copy2(root / name, area / name)
if mode == "duplex":
    impl = area / files[0]
    text = impl.read_text()
    old = "socket.connectToServer(name, QIODevice::WriteOnly);"
    assert text.count(old) == 1
    impl.write_text(text.replace(old, "socket.connectToServer(name, QIODevice::ReadWrite);"))
if latency == "delayed":
    fixtures = tests / "paymentipctests.cpp"
    text = fixtures.read_text()
    old = "    sender->start();"
    assert text.count(old) == 1
    fixtures.write_text(text.replace(old, old + "\n    QThread::msleep(60);"))
(area / "main.cpp").write_text("#include <qt/test/paymentipctests.h>\n#include <QTest>\nQTEST_GUILESS_MAIN(PaymentIpcTests)\n")
(area / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.22)
project(WalletPaymentTests LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Network Test)
add_executable(wallet-payment-tests main.cpp src/qt/paymentipc.cpp src/qt/test/paymentipctests.cpp src/qt/test/paymentipctests.h)
target_include_directories(wallet-payment-tests PRIVATE src)
target_link_libraries(wallet-payment-tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
""")
env = dict(os.environ, QT_FORCE_STDERR_LOGGING="1", PATH=str(qt / "bin") + os.pathsep + os.environ["PATH"])
subprocess.run(["cmake", "-S", str(area), "-B", str(area / "build"), "-G", "Visual Studio 17 2022", "-A", "x64",
                f"-DCMAKE_PREFIX_PATH={qt}"], check=True, env=env)
subprocess.run(["cmake", "--build", str(area / "build"), "--config", "Release", "--parallel", "4"], check=True, env=env)
for attempt in range(1, 4):
    print(f"{mode}, {latency}, run {attempt}",flush=True)
    run = subprocess.run([str(area / "build/Release/wallet-payment-tests.exe")],cwd=area,env=env,capture_output=True,timeout=60)
    data = run.stdout + run.stderr
    print(data.decode(errors="replace"),flush=True)
    (area / f"qt-{attempt}.log").write_bytes(data)
    if run.returncode:
        raise SystemExit(run.returncode)
