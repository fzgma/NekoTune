"""Build community templates from an extracted SDK, without host source dependencies."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


def run(*arguments: str) -> None:
    subprocess.run(arguments, check=True)


def smoke_sdk(archive: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="nekotune-sdk-smoke-") as temporary:
        root = Path(temporary)
        extracted = root / "sdk"
        with zipfile.ZipFile(archive) as package:
            package.extractall(extracted)
        # Exercise relocatable CMake exports, not the original installation prefix.
        sdk = root / "relocated-sdk"
        extracted.rename(sdk)
        templates = sdk / "share/NekoTuneSDK/templates"
        for name in ("native", "qml"):
            source = root / name
            shutil.copytree(templates / name, source)
            if not (source / ".github/workflows/build.yml").is_file():
                raise RuntimeError(f"{name}: workflow missing from SDK archive")
            build = root / f"build-{name}"
            options = (
                ["-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"] if name == "native" else []
            )
            run(
                "cmake",
                "-S",
                str(source),
                "-B",
                str(build),
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_PREFIX_PATH={sdk}",
                *options,
            )
            run("cmake", "--build", str(build), "--parallel", "2")
            if name == "native":
                run(
                    "ctest",
                    "--test-dir",
                    str(build),
                    "--output-on-failure",
                    "--no-tests=error",
                )
            packages = list((build / "packages").glob("*.zip"))
            if len(packages) != 1:
                raise RuntimeError(f"{name}: expected exactly one extension package")
            with zipfile.ZipFile(packages[0]) as package:
                manifest = json.loads(package.read("extension.json"))
                if "Page.qml" not in package.namelist():
                    raise RuntimeError(f"{name}: QML missing from package")
                if name == "native":
                    platform = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
                    if manifest["library"][platform] not in package.namelist():
                        raise RuntimeError("Native library missing from package")
                elif "library" in manifest:
                    raise RuntimeError(
                        "Pure QML package unexpectedly requires a native library"
                    )
        print("SDK relocation, native plugin loading and both template packages passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    smoke_sdk(parser.parse_args().archive)
