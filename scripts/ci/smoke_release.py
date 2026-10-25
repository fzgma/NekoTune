"""Run the installed desktop app with isolated data and no build-tool search paths.

Usage: smoke_release.py <stage> [--wayland]
"""

from __future__ import annotations

import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path

WINDOWS = sys.platform == "win32"


class Channel:
    """Newline-delimited JSON over a Unix socket or a Windows named pipe.

    Reads are polled from the caller's thread: Windows serializes I/O on a synchronous pipe
    handle, so a blocked reader thread would also block every write.
    """

    def __init__(self, endpoint: str) -> None:
        self._buffer = b""
        if WINDOWS:
            import msvcrt

            self._pipe = open(endpoint, "r+b", buffering=0)  # noqa: SIM115
            self._handle = msvcrt.get_osfhandle(self._pipe.fileno())
            self._socket = None
        else:
            self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._socket.connect(endpoint)
            self._pipe = None

    def _available(self, timeout: float) -> bytes:
        if WINDOWS:
            import _winapi

            until = time.monotonic() + timeout
            while True:
                available = _winapi.PeekNamedPipe(self._handle, 0)[0]
                if available:
                    return self._pipe.read(available)
                if time.monotonic() >= until:
                    return b""
                time.sleep(0.02)
        self._socket.settimeout(timeout)
        try:
            chunk = self._socket.recv(65536)
        except TimeoutError:
            return b""
        if not chunk:
            raise ConnectionError("IPC disconnected")
        return chunk

    def receive(self, timeout: float) -> dict | None:
        until = time.monotonic() + timeout
        while b"\n" not in self._buffer:
            remaining = until - time.monotonic()
            if remaining <= 0:
                return None
            self._buffer += self._available(remaining)
        line, self._buffer = self._buffer.split(b"\n", 1)
        return json.loads(line)

    def send(self, message: dict) -> None:
        data = json.dumps(message).encode() + b"\n"
        if self._pipe:
            self._pipe.write(data)
        else:
            self._socket.sendall(data)

    def close(self) -> None:
        try:
            (self._pipe or self._socket).close()
        except OSError:
            pass


class Process:
    def __init__(self, arguments: list[str], environment: dict[str, str]) -> None:
        self.output: list[str] = []
        flags = subprocess.CREATE_NO_WINDOW if WINDOWS else 0
        self.child = subprocess.Popen(arguments, env=environment, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, creationflags=flags)
        threading.Thread(target=self._collect, daemon=True).start()

    def _collect(self) -> None:
        for line in self.child.stdout:
            self.output.append(line.decode(errors="replace"))

    @property
    def exited(self) -> bool:
        return self.child.poll() is not None

    def text(self) -> str:
        return "".join(self.output)

    def stop(self) -> None:
        if self.exited:
            return
        self.child.terminate()
        try:
            self.child.wait(5)
        except subprocess.TimeoutExpired:
            self.child.kill()
            self.child.wait()


def check_distribution(stage: Path, native_wayland: bool, backend_only: bool, input_method: str = "",
                       standalone: bool = False) -> None:
    label = ("Standalone frontend + external backend" if standalone else "Backend" if backend_only
             else f"Desktop{f' ({input_method})' if input_method else ''}")
    input_plugin = ""
    if input_method:
        name = "libfcitx5platforminputcontextplugin.so" if input_method == "fcitx" else "libibusplatforminputcontextplugin.so"
        input_plugin = str((stage / "plugins/platforminputcontexts" / name).resolve())
    root = Path(tempfile.mkdtemp(prefix="nekotune-release-"))
    socket_name = f"nekotune-release-{uuid.uuid4()}" if WINDOWS else str(root / "backend.sock")
    endpoint = rf"\\.\pipe\{socket_name}" if WINDOWS else socket_name
    environment = {key: value for key, value in os.environ.items()
                   if key not in {"LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                                  "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_DIR", "QTDIR"}
                   and key.lower() != "path"}
    system_root = os.environ.get("SystemRoot", r"C:\Windows")
    environment.update({
        "NEKOTUNE_HOME": str(root), "NEKOTUNE_SOCKET": socket_name,
        "NEKOTUNE_DB_PATH": str(root / "player.sqlite3"),
        "XDG_CONFIG_HOME": str(root / "config"), "XDG_DATA_HOME": str(root / "data"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "QT_QPA_PLATFORM": "windows" if WINDOWS else
        "wayland" if native_wayland and (not backend_only or standalone) else "offscreen",
        "QT_QUICK_BACKEND": "software",
        "QT_FORCE_STDERR_LOGGING": "1",
        "PATH": f"{stage}/bin;{system_root}/System32;{system_root}" if WINDOWS else "/usr/bin:/bin",
        "NEKOTUNE_SAFE_MODE": "1",
    })
    if input_method:
        environment["QT_IM_MODULE"] = input_method
        environment["QT_DEBUG_PLUGINS"] = "1"
        environment.pop("QT_IM_MODULES", None)
    if backend_only and not WINDOWS:
        environment["LD_LIBRARY_PATH"] = str(stage / "lib")
    exe = ".exe" if WINDOWS else ""
    executable = (stage / "bin" / f"nekotune-backend{exe}" if backend_only
                  else stage / "bin" / "nekotune.exe" if WINDOWS else stage / "NekoTune")
    test_wallpaper = native_wayland and (not backend_only or standalone)
    if test_wallpaper:
        (root / "config").mkdir(parents=True, exist_ok=True)
        (root / "config/settings.json").write_text(
            json.dumps({"wallpaper": {"enabled": True, "autoRestore": True}}), encoding="utf-8")
        environment["NEKOTUNE_SAFE_MODE"] = "0"
        environment.pop("QT_QUICK_BACKEND", None)
    safe = [] if backend_only or test_wallpaper else ["--safe-mode"]
    child = Process([str(executable), *safe], environment)
    frontend = None
    if standalone:
        frontend = Process([str(stage / "bin" / f"nekotune-qt-qml{exe}"), *([] if test_wallpaper else ["--safe-mode"])],
                           environment)
    trace: list[str] = []

    def diagnostics() -> str:
        status = "running" if not child.exited else f"exit={child.child.returncode}"
        return f"{label}: {status}\nIPC: " + "\n".join(trace[-20:]) + "\n" + child.text() + (frontend.text() if frontend else "")

    channel = None
    try:
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            assert not child.exited, f"Process exited during startup:\n{diagnostics()}"
            try:
                channel = Channel(endpoint)
                break
            except OSError:
                time.sleep(0.15)
        assert channel, f"IPC startup timed out:\n{diagnostics()}"
        trace.append("pipe connected")
        next_id = 0

        def call(method: str, params: dict | None = None) -> dict:
            nonlocal next_id
            assert not child.exited, diagnostics()
            next_id += 1
            request = next_id
            trace.append(f"send: {request} {method}")
            channel.send({"id": request, "method": method, "params": params or {}})
            until = time.monotonic() + 5
            while True:
                try:
                    message = channel.receive(max(0.0, until - time.monotonic()))
                except (OSError, ConnectionError):
                    raise AssertionError(f"IPC disconnected during {method}\n{diagnostics()}") from None
                if message is None:
                    raise AssertionError(f"IPC timeout: {method}\n{diagnostics()}")
                trace.append(f"received: {json.dumps(message)[:500]}")
                if message.get("id") == request:
                    assert message.get("status") == "ok", json.dumps(message)
                    return message.get("data", {})

        assert call("player.status")
        extensions: dict = {}
        while time.monotonic() < deadline:
            extensions = call("extensions.list")
            if extensions.get("runtime", {}).get("catalogueReady"):
                break
            time.sleep(0.15)
        assert extensions.get("runtime", {}).get("catalogueReady") is True, f"Extension runtime failed:\n{diagnostics()}"
        selection = call("appearance.select", {"slot": "theme", "contribution": "builtin/ocean"})
        assert selection["selections"]["theme"] == "builtin/ocean"
        theme_path = root / "ocean.nktheme.zip"
        call("appearance.export_theme", {"contribution": "builtin/ocean", "path": str(theme_path)})
        assert struct.unpack("<I", theme_path.read_bytes()[:4])[0] == 0x04034B50
        imported = call("extensions.install", {"path": str(theme_path), "themeOnly": True})
        assert imported["extension"]["type"] == "theme"
        assert imported["extension"]["hasCode"] is False
        call("appearance.select", {"slot": "theme", "contribution": ""})
        time.sleep(1.5)
        assert not child.exited, f"Process exited after IPC startup:\n{diagnostics()}"
        output = child.text() + (frontend.text() if frontend else "")
        assert not re.search(r'QQmlApplicationEngine failed|module "[^"]+" is not installed', output), diagnostics()
        assert not re.search(r"TypeError:|ReferenceError:|Binding loop detected", output), diagnostics()
        if input_method:
            assert f'"{input_plugin}" loaded library' in output, \
                f"Packaged {input_method} input context was not loaded:\n{diagnostics()}"
        if test_wallpaper:
            assert re.search(r"NekoTune wallpaper mapped on", output), diagnostics()
        if frontend:
            assert not frontend.exited, f"Standalone frontend exited early:\n{diagnostics()}"
            frontend.stop()
            # The external backend and its IPC remain alive after frontend removal.
            assert call("player.status")
        print(f"Installed {label}, SQLite, IPC, native extension runtime and theme import/export passed.")
    finally:
        if frontend:
            frontend.stop()
        if channel:
            channel.close()
        child.stop()
        for _ in range(20):
            try:
                shutil.rmtree(root)
                break
            except FileNotFoundError:
                break
            except OSError:
                time.sleep(0.15)


def main() -> None:
    stage = Path(sys.argv[1]).resolve()
    native_wayland = "--wayland" in sys.argv[2:]
    runtime = json.loads((stage / "wallpaper-runtime.json").read_text(encoding="utf-8"))
    if runtime.get("backend") == "wayland":
        for file in ("plugins/wayland-shell-integration/libxdg-shell.so", "qml/QtQuick/Effects/qmldir"):
            assert (stage / file).exists(), f"Missing {file}"
    if native_wayland:
        assert runtime.get("backend") == "wayland"
    checks = ([(True, "", False), (False, "fcitx", False), (False, "ibus", False), (True, "", True)]
              if sys.platform.startswith("linux") else [(True, "", False), (False, "", False), (True, "", True)])
    failures = []
    for backend_only, input_method, standalone in checks:
        try:
            check_distribution(stage, native_wayland, backend_only, input_method, standalone)
        except Exception as error:  # noqa: BLE001 - report every distribution check
            print(error, file=sys.stderr)
            failures.append(error)
    if failures:
        raise SystemExit(f"Installed distribution validation failed ({len(failures)} checks)")


if __name__ == "__main__":
    main()
