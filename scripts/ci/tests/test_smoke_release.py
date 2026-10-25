"""Exercise distribution smoke checks against the current IPC contract."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile


spec = importlib.util.spec_from_file_location(
    "smoke_release", Path(__file__).resolve().parents[1] / "smoke_release.py"
)
smoke_release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke_release)


class DistributionProtocolTest(unittest.TestCase):
    def test_runtime_readiness_and_theme_round_trip(self) -> None:
        selections: dict[str, str] = {}
        catalogue_states = iter((False, True))
        requests: list[str] = []

        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(smoke_release, "Process") as process,
            patch.object(smoke_release, "Channel") as channel,
            patch.object(smoke_release.time, "sleep"),
        ):
            process.return_value.exited = False
            process.return_value.text.return_value = ""
            connection = channel.return_value

            def respond(message: dict) -> None:
                method = message["method"]
                params = message["params"]
                requests.append(method)
                if method == "player.status":
                    data = {"state": "stopped"}
                elif method == "extensions.list":
                    data = {
                        "runtime": {"catalogueReady": next(catalogue_states)},
                        "extensions": [],
                    }
                elif method == "appearance.select":
                    selections[params["slot"]] = params["contribution"]
                    data = {"selections": dict(selections)}
                elif method == "appearance.export_theme":
                    self.assertEqual(selections["theme"], "builtin/ocean")
                    with zipfile.ZipFile(params["path"], "w") as package:
                        package.writestr("extension.json", "{}")
                    data = {}
                elif method == "extensions.install":
                    self.assertTrue(params["themeOnly"])
                    with zipfile.ZipFile(params["path"]) as package:
                        self.assertIn("extension.json", package.namelist())
                    data = {"extension": {"type": "theme", "hasCode": False}}
                else:
                    self.fail(f"Unsupported IPC method: {method}")
                connection.receive.return_value = {
                    "id": message["id"], "status": "ok", "data": data,
                }

            connection.send.side_effect = respond
            smoke_release.check_distribution(
                Path(directory), native_wayland=False, backend_only=True
            )
            self.assertEqual(selections["theme"], "")
            self.assertEqual(requests.count("extensions.list"), 2)
            connection.close.assert_called_once()
            process.return_value.stop.assert_called_once()


if __name__ == "__main__":
    unittest.main()
