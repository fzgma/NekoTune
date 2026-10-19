"""Bundle installed NekoTune binaries and their Qt/native runtime dependencies."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile


def run(*command: str, env: dict[str, str] | None = None) -> str:
    return subprocess.check_output(command, text=True, env=env, errors="replace")


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source.resolve(), destination)


def copy_linux_plugins(stage: Path, roots: list[Path]) -> None:
    for root in roots:
        if not root.is_dir():
            raise RuntimeError(f"Qt plugin directory does not exist: {root}")
        for kind in (
            "platforms", "platforminputcontexts", "xcbglintegrations", "imageformats",
            "tls", "sqldrivers", "multimedia", "styles", "iconengines",
            "networkinformation",
            "wayland-decoration-client", "wayland-graphics-integration-client",
            "wayland-shell-integration", "wayland-inputdevice-integration",
        ):
            for plugin in (root / kind).glob("*.so"):
                if kind == "sqldrivers" and plugin.name != "libqsqlite.so":
                    continue
                if kind == "platforms" and plugin.name not in {
                    "libqxcb.so", "libqoffscreen.so", "libqminimal.so",
                    "libqwayland.so", "libqwayland-egl.so", "libqwayland-generic.so",
                }:
                    continue
                if kind == "imageformats" and not plugin.name.startswith("libq"):
                    continue
                copy_file(plugin, stage / "plugins" / kind / plugin.name)
    for name in (
        "libfcitx5platforminputcontextplugin.so", "libibusplatforminputcontextplugin.so",
    ):
        plugin = stage / "plugins/platforminputcontexts" / name
        if not plugin.is_file():
            raise RuntimeError(f"Required Linux input method plugin is missing: {plugin}")


def copy_fcitx_licenses(stage: Path, source: Path) -> None:
    licenses = source / "LICENSES"
    if not licenses.is_dir() or not any(licenses.glob("*.txt")):
        raise RuntimeError(f"Fcitx5-Qt license files are missing: {licenses}")
    shutil.copytree(licenses, stage / "licenses/Fcitx5-Qt", dirs_exist_ok=True)


def linux_dependencies(binary: Path, environment: dict[str, str], *,
                       relocations: bool = False) -> str:
    command = ["ldd", "-r"] if relocations else ["ldd"]
    output = run(*command, str(binary), env=environment)
    if ("=> not found" in output or "undefined symbol:" in output
            or "version `" in output and "not found" in output):
        raise RuntimeError(f"Unresolved dependency in {binary}:\n{output}")
    return output


def bundle_linux(stage: Path, qt: Path, extra_library_dirs: list[Path],
                 extra_plugin_dirs: list[Path]) -> None:
    validate_build_qt(stage, qt)
    # Multimedia also installs optional Quick3D modules without their 3D dependencies.
    # Ship the host's 2D UI modules and the multimedia API used by extensions.
    for module in ("QtQml", "QtQuick", "Qt", "QtMultimedia"):
        source = qt / "qml" / module
        if source.exists():
            shutil.copytree(source, stage / "qml" / module, dirs_exist_ok=True)
    # All plugins must be present before scanning ELF dependencies. Extra plugins
    # must be built against this Qt, never copied from a newer system Qt.
    copy_linux_plugins(stage, [qt / "plugins", *extra_plugin_dirs])
    library_dir = stage / "lib"
    library_dir.mkdir(exist_ok=True)
    # The loader, libc and graphics drivers belong to the target OS.
    system_library = re.compile(
        r"^(ld-linux|lib(c|m|dl|pthread|rt|resolv|util|nss_[^.]+)\.so"
        r"|lib(GL|EGL|GLX|GLdispatch|OpenGL|drm|gbm|vulkan)\.so)"
    )
    environment = dict(os.environ)
    environment["LD_LIBRARY_PATH"] = os.pathsep.join(
        [str(library_dir), str(qt / "lib"), *map(str, extra_library_dirs),
         environment.get("LD_LIBRARY_PATH", "")]
    )
    pending = [file for file in stage.rglob("*") if file.is_file()
               and (".so" in file.name or file.parent.name == "bin")]
    entry_points = [binary for binary in pending if not binary.is_relative_to(library_dir)]
    seen: set[Path] = set()
    while pending:
        binary = pending.pop()
        if binary in seen:
            continue
        with binary.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        seen.add(binary)
        output = linux_dependencies(binary, environment)
        for name, filename in re.findall(r"^\s*(\S+) => (/\S+)", output, re.MULTILINE):
            if system_library.match(name):
                continue
            if name.startswith("libQt6"):
                validate_qt_library(Path(filename), qt / "lib" / name)
            destination = library_dir / name
            if not destination.exists():
                copy_file(Path(filename), destination)
                pending.append(destination)
    # Qt's FFmpeg libavutil references RAND_bytes from the sibling SSL stub.
    # Validate symbols in the executable/plugin's complete load scope; isolated
    # leaf libraries need not provide every symbol through their own DT_NEEDED.
    for binary in entry_points:
        if binary in seen:
            linux_dependencies(binary, environment, relocations=True)
    launcher = stage / "NekoTune"
    launcher.write_text(
        '#!/bin/sh\nset -eu\n'
        'app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        'export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
        'export QT_PLUGIN_PATH="$app_dir/plugins"\n'
        'export QML_IMPORT_PATH="$app_dir/qml"\n'
        'export QML2_IMPORT_PATH="$app_dir/qml"\n'
        'exec "$app_dir/bin/nekotune" "$@"\n', encoding="utf-8"
    )
    launcher.chmod(0o755)
    (stage / "bin" / "qt.conf").write_text(
        "[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\n",
        encoding="utf-8",
    )


def validate_build_qt(stage: Path, qt: Path) -> None:
    """Private QtWayland ABI must match the Qt used to build the application."""
    manifest = json.loads((stage / "wallpaper-runtime.json").read_text())
    for name in ("qmake", "qmake6"):
        tool = qt / "bin" / name
        if tool.is_file():
            version = run(str(tool), "-query", "QT_VERSION").strip()
            if version != manifest["qt"]:
                raise RuntimeError(f"Build Qt {manifest['qt']} differs from package Qt {version}")
            return
    raise RuntimeError("Cannot verify package Qt version: qmake is missing")


def validate_qt_library(actual: Path, expected: Path) -> None:
    if not expected.is_file():
        raise RuntimeError(f"Qt dependency is outside the selected Qt distribution: {actual}")
    if actual.resolve() == expected.resolve():
        return
    with actual.open("rb") as source, expected.open("rb") as reference:
        if hashlib.file_digest(source, "sha256").digest() != hashlib.file_digest(reference, "sha256").digest():
            raise RuntimeError(f"Mixed Qt ABI: {actual} does not match {expected}")


def validate_wallpaper_runtime(stage: Path) -> None:
    manifest = json.loads((stage / "wallpaper-runtime.json").read_text())
    if manifest["backend"] != "wayland":
        return
    requirements = (
        "lib/libLayerShellQtInterface.so*", "lib/libQt6WaylandClient.so*",
        "plugins/platforms/libqwayland*.so", "plugins/wayland-shell-integration/libxdg-shell.so",
        "plugins/wayland-graphics-integration-client/*.so", "qml/QtQuick/Effects/qmldir",
    )
    for pattern in requirements:
        if not any(path.is_file() for path in stage.glob(pattern)):
            raise RuntimeError(f"Incomplete Wayland wallpaper runtime: {pattern}")


def bundle_windows(stage: Path, qt: Path) -> None:
    binary_dir = stage / "bin"
    deploy = qt / "bin" / "windeployqt6.exe"
    if not deploy.exists():
        deploy = qt / "bin" / "windeployqt.exe"
    print(run(str(deploy), "--release", "--no-translations", "--qmldir",
              str(Path("frontend/qt-qml/qml").resolve()), "--dir", str(binary_dir),
              "--qml-deploy-dir", str(binary_dir / "qml"),
              str(binary_dir / "nekotune.exe")))
    # MSYS2 Qt's compiled-in QML path is share/qt6/qml. windeployqt copies
    # imports into bin/qml, so make the installed layout explicit for Qt.
    (binary_dir / "qt.conf").write_text(
        "[Paths]\nPrefix=.\nBinaries=.\nLibraries=.\nPlugins=.\nQmlImports=qml\n",
        encoding="utf-8",
    )
    controls = binary_dir / "qml" / "QtQuick" / "Controls"
    if not any(controls.glob("*qtquickcontrols2plugin*.dll")):
        raise RuntimeError(f"QtQuick.Controls plugin was not deployed into {controls}")
    # windeployqt copies every SQL plugin, including drivers the app never uses.
    for driver in (binary_dir / "sqldrivers").glob("*.dll"):
        if driver.name.lower() != "qsqlite.dll":
            driver.unlink()
    sources = {file.name.lower(): file for file in (qt / "bin").glob("*.dll")}
    windows = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"
    system_dlls = {file.name.lower() for file in windows.glob("*.dll")}
    pending = [file for file in stage.rglob("*")
               if file.suffix.lower() in {".exe", ".dll"}]
    seen: set[Path] = set()
    while pending:
        binary = pending.pop()
        if binary in seen:
            continue
        seen.add(binary)
        output = run("objdump", "-p", str(binary))
        for name in re.findall(r"DLL Name:\s*(\S+)", output):
            key = name.lower()
            destination = binary_dir / name
            if destination.exists():
                continue
            if key in sources:
                copy_file(sources[key], destination)
                pending.append(destination)
            elif key not in system_dlls and not key.startswith(("api-ms-", "ext-ms-")):
                raise RuntimeError(f"Unresolved dependency {name} in {binary}")


def validate_audio_runtime(stage: Path, platform: str) -> None:
    """Reject archives missing a playback/DSP library or the Qt fallback plugin."""
    library_dir = stage / ("lib" if platform == "linux" else "bin")
    for component in ("avformat", "avcodec", "avutil", "swresample", "avfilter"):
        pattern = f"lib{component}.so*" if platform == "linux" else f"{component}-*.dll"
        if not any(path.is_file() for path in library_dir.glob(pattern)):
            raise RuntimeError(f"Incomplete audio runtime: {component} missing from {library_dir}")
    plugins = stage / "plugins/multimedia" if platform == "linux" else stage / "bin/multimedia"
    pattern = "*.so" if platform == "linux" else "*.dll"
    if not any(path.is_file() for path in plugins.glob(pattern)):
        raise RuntimeError(f"Qt Multimedia fallback plugin missing from {plugins}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--library-dir", type=Path, action="append", default=[])
    parser.add_argument("--plugin-dir", type=Path, action="append", default=[],
                        help="Additional plugin root, built with --qt-prefix")
    parser.add_argument("--fcitx-source", type=Path,
                        help="Fcitx5-Qt source tree supplying bundled license notices")
    parser.add_argument("--layershell-source", type=Path,
                        help="LayerShellQt source tree built against --qt-prefix")
    parser.add_argument("--platform", choices=["linux", "windows"], required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    stage = args.stage.resolve()
    qt = args.qt_prefix.resolve()
    if args.platform == "linux":
        if args.fcitx_source is None:
            parser.error("--fcitx-source is required for Linux distributions")
        copy_fcitx_licenses(stage, args.fcitx_source.resolve())
        bundle_linux(stage, qt, [directory.resolve() for directory in args.library_dir],
                     [directory.resolve() for directory in args.plugin_dir])
        if json.loads((stage / "wallpaper-runtime.json").read_text())["backend"] == "wayland":
            if args.layershell_source is None or not (args.layershell_source / "LICENSES").is_dir():
                parser.error("--layershell-source with LICENSES is required for Wayland wallpaper distributions")
            shutil.copytree(args.layershell_source / "LICENSES", stage / "licenses/LayerShellQt", dirs_exist_ok=True)
    else:
        bundle_windows(stage, qt)
    validate_audio_runtime(stage, args.platform)
    validate_wallpaper_runtime(stage)
    licenses = stage / "licenses"
    licenses.mkdir(exist_ok=True)
    for filename in ("LICENSE", "README.md"):
        if Path(filename).exists():
            copy_file(Path(filename), stage / filename)
    # Preserve notices from the actual installed distributions.
    for source in (qt / "share/licenses", qt / "Licenses", Path(".ci-qtkeychain/COPYING")):
        if source.is_dir():
            shutil.copytree(source, licenses / source.name, dirs_exist_ok=True)
        elif source.is_file():
            copy_file(source, licenses / "QtKeychain-LICENSE")
    fcitx_notice = (
        "Fcitx5-Qt 5.1.9 (BSD/LGPL): licenses/Fcitx5-Qt/\n"
        "Source: https://github.com/fcitx/fcitx5-qt/tree/3ade1a680221f3e211adcfb50c323285c4bded77\n"
        if args.platform == "linux" else ""
    )
    (licenses / "THIRD-PARTY.md").write_text(
        "This distribution includes Qt (LGPL/GPL), QtKeychain (BSD), "
        "FFmpeg (avformat, avcodec, avutil, swresample, avfilter), Node.js and their dependencies.\n"
        "Node.js notices are in libexec/nekotune/NODE-LICENSE.\n"
        "Qt source and licenses: https://code.qt.io/ and https://www.qt.io/licensing/\n"
        "QtKeychain source: https://github.com/frankosterfeld/qtkeychain/tree/0.17.0\n"
        "FFmpeg source and licenses: https://ffmpeg.org/legal.html\n"
        "MSYS2 package sources and notices: https://github.com/msys2/MINGW-packages\n"
        "Optional LayerShellQt (LGPL): licenses/LayerShellQt; https://invent.kde.org/plasma/layer-shell-qt\n"
        + fcitx_notice,
        encoding="utf-8",
    )
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"NekoTune-{args.version}-{args.platform}-x64"
    if args.platform == "windows":
        archive = args.output / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for file in sorted(stage.rglob("*")):
                if file.is_file():
                    output.write(file, Path(name) / file.relative_to(stage))
    else:
        archive = args.output / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as output:
            output.add(stage, arcname=name)
    with archive.open("rb") as stream:
        checksum = hashlib.file_digest(stream, "sha256").hexdigest()
    archive.with_name(archive.name + ".sha256").write_text(
        f"{checksum}  {archive.name}\n", encoding="utf-8"
    )
    print(f"Packaged {archive} ({archive.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
