"""Export the SDK component from the same build that produced the host."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path


def package_sdk(build: Path, output: Path, version: str, platform: str) -> Path:
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", version):
        raise ValueError("Expected a SemVer host version")
    with tempfile.TemporaryDirectory(prefix="nekotune-sdk-") as temporary:
        stage = Path(temporary) / "sdk"
        subprocess.run(
            [
                "cmake",
                "--install",
                str(build.resolve()),
                "--prefix",
                str(stage),
                "--component",
                "NekoTuneSDK",
                "--config",
                "Release",
            ],
            check=True,
        )
        info = json.loads((stage / "share/NekoTuneSDK/build-info.json").read_text())
        expected_system = {"linux": "Linux", "windows": "Windows"}[platform]
        if info["system"] != expected_system:
            raise ValueError(f"SDK system {info['system']} differs from {platform}")
        if info["processor"].lower() not in {"amd64", "x64", "x86_64"}:
            raise ValueError("Release SDK packages currently support x86_64 only")
        if info["hostVersion"] and info["hostVersion"] != version:
            raise ValueError("SDK host version differs from the requested release")
        output.mkdir(parents=True, exist_ok=True)
        archive = output / f"NekoTuneSDK-{version}-{platform}-x64.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
            for file in sorted(stage.rglob("*")):
                if file.is_file():
                    package.write(file, file.relative_to(stage).as_posix())
        with archive.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        archive.with_suffix(".zip.sha256").write_text(
            f"{digest}  {archive.name}\n", encoding="utf-8"
        )
        return archive


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", choices=("linux", "windows"), required=True)
    args = parser.parse_args()
    print(package_sdk(args.build, args.output_dir, args.version, args.platform))


if __name__ == "__main__":
    main()
