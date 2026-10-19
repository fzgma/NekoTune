"""Regression coverage for Linux input method deployment."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "package_release", Path(__file__).resolve().parents[1] / "package_release.py"
)
package_release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_release)


class LinuxInputMethodPackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "stage"
        self.qt = self.root / "qt/plugins"
        self.extra = self.root / "extra/plugins"

    def plugin(
        self, root: Path, kind: str, name: str, content: bytes = b"plugin"
    ) -> Path:
        file = root / kind / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(content)
        return file

    def test_extra_input_context_is_deployed_with_existing_plugins(self) -> None:
        self.plugin(
            self.qt, "platforminputcontexts", "libibusplatforminputcontextplugin.so"
        )
        self.plugin(
            self.extra,
            "platforminputcontexts",
            "libfcitx5platforminputcontextplugin.so",
            b"release-qt",
        )
        self.plugin(self.qt, "platforms", "libqxcb.so")
        self.plugin(self.qt, "platforms", "libqwayland.so")
        self.plugin(self.qt, "sqldrivers", "libqsqlite.so")
        self.plugin(self.qt, "sqldrivers", "libqsqlmysql.so")
        package_release.copy_linux_plugins(self.stage, [self.qt, self.extra])
        deployed = self.stage / "plugins"
        self.assertEqual(
            (
                deployed
                / "platforminputcontexts/libfcitx5platforminputcontextplugin.so"
            ).read_bytes(),
            b"release-qt",
        )
        self.assertTrue(
            (
                deployed / "platforminputcontexts/libibusplatforminputcontextplugin.so"
            ).exists()
        )
        self.assertTrue((deployed / "platforms/libqxcb.so").exists())
        self.assertTrue((deployed / "platforms/libqwayland.so").exists())
        self.assertTrue((deployed / "sqldrivers/libqsqlite.so").exists())
        self.assertFalse((deployed / "sqldrivers/libqsqlmysql.so").exists())

    def test_missing_input_context_fails_packaging(self) -> None:
        for missing, present in (("fcitx5", "ibus"), ("ibus", "fcitx5")):
            with self.subTest(missing=missing):
                root = self.root / missing
                self.plugin(
                    root,
                    "platforminputcontexts",
                    f"lib{present}platforminputcontextplugin.so",
                )
                with self.assertRaisesRegex(
                    RuntimeError, f"lib{missing}platforminputcontextplugin"
                ):
                    package_release.copy_linux_plugins(
                        self.root / f"stage-{missing}", [root]
                    )

    def test_fcitx_notices_are_required_and_preserved(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "license files are missing"):
            package_release.copy_fcitx_licenses(self.stage, self.root / "source")
        notice = self.root / "source/LICENSES/BSD-3-Clause.txt"
        notice.parent.mkdir(parents=True)
        notice.write_text("Fcitx notice", encoding="utf-8")
        package_release.copy_fcitx_licenses(self.stage, self.root / "source")
        self.assertEqual(
            (self.stage / "licenses/Fcitx5-Qt/BSD-3-Clause.txt").read_text(),
            "Fcitx notice",
        )


class AudioRuntimePackagingTest(unittest.TestCase):
    def test_audio_libraries_and_qt_fallback_are_required(self) -> None:
        for platform in ("linux", "windows"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                stage = Path(directory)
                library_dir = stage / ("lib" if platform == "linux" else "bin")
                library_dir.mkdir()
                libraries = []
                for component in ("avformat", "avcodec", "avutil", "swresample", "avfilter"):
                    name = f"lib{component}.so.1" if platform == "linux" else f"{component}-1.dll"
                    library = library_dir / name
                    library.write_bytes(b"library")
                    libraries.append(library)
                plugin = stage / ("plugins/multimedia/libffmpegmediaplugin.so" if platform == "linux"
                                  else "bin/multimedia/ffmpegmediaplugin.dll")
                plugin.parent.mkdir(parents=True)
                plugin.write_bytes(b"plugin")
                package_release.validate_audio_runtime(stage, platform)
                for library in libraries:
                    library.unlink()
                    with self.assertRaisesRegex(RuntimeError, "Incomplete audio runtime"):
                        package_release.validate_audio_runtime(stage, platform)
                    library.write_bytes(b"library")
                plugin.unlink()
                with self.assertRaisesRegex(RuntimeError, "fallback plugin missing"):
                    package_release.validate_audio_runtime(stage, platform)


class WallpaperRuntimePackagingTest(unittest.TestCase):
    def test_wayland_dependencies_are_required_only_when_enabled(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            manifest = stage / "wallpaper-runtime.json"
            manifest.write_text(json.dumps({"backend": "disabled", "qt": "6.11.2"}))
            package_release.validate_wallpaper_runtime(stage)
            manifest.write_text(json.dumps({"backend": "wayland", "qt": "6.11.2"}))
            files = ["lib/libLayerShellQtInterface.so.6", "lib/libQt6WaylandClient.so.6",
                     "plugins/platforms/libqwayland.so", "plugins/wayland-shell-integration/libxdg-shell.so",
                     "plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so",
                     "qml/QtQuick/Effects/qmldir"]
            for filename in files:
                with self.assertRaisesRegex(RuntimeError, "Incomplete Wayland"):
                    package_release.validate_wallpaper_runtime(stage)
                path = stage / filename
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"runtime")
            package_release.validate_wallpaper_runtime(stage)

    def test_mixed_qt_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected, actual = root / "qt.so", root / "system.so"
            expected.write_bytes(b"release Qt")
            actual.write_bytes(b"system Qt")
            with self.assertRaisesRegex(RuntimeError, "Mixed Qt ABI"):
                package_release.validate_qt_library(actual, expected)
            actual.write_bytes(expected.read_bytes())
            package_release.validate_qt_library(actual, expected)
            (root / "wallpaper-runtime.json").write_text(json.dumps({"backend": "wayland", "qt": "6.11.2"}))
            (root / "bin").mkdir()
            (root / "bin/qmake").touch()
            with patch.object(package_release, "run", return_value="6.10.0"):
                with self.assertRaisesRegex(RuntimeError, "differs from package Qt"):
                    package_release.validate_build_qt(root, root)


if __name__ == "__main__":
    unittest.main()
