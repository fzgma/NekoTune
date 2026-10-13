"""Regression coverage for Linux input method deployment."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


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
        self.assertFalse((deployed / "platforms/libqwayland.so").exists())
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


if __name__ == "__main__":
    unittest.main()
