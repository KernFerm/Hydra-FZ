import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bridge", ROOT / "companion" / "hydra_fz_bridge.py")
bridge = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bridge
SPEC.loader.exec_module(bridge)


class CompanionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.temp.name)
        self.config = root / "config.json"
        self.candidates = root / "candidates.txt"
        self.output = root / "report.txt"
        self.candidates.write_text("wrong\ncorrect\n", encoding="utf-8")
        self.patchers = [
            mock.patch.object(bridge, "CONFIG", self.config),
            mock.patch.object(bridge, "CANDIDATES", self.candidates),
            mock.patch.object(bridge, "REPORT", self.output),
            mock.patch.object(bridge, "OUTPUT", self.output.parent),
        ]
        for patcher in self.patchers:
            patcher.start()

    def tearDown(self):
        for patcher in reversed(self.patchers):
            patcher.stop()
        self.temp.cleanup()

    def write_config(self, **changes):
        data = {"host": "127.0.0.1", "service": "ssh", "port": 2222, "login": "labuser"}
        data.update(changes)
        self.config.write_text(json.dumps(data), encoding="utf-8")

    def test_loopback_job_and_fixed_arguments(self):
        self.write_config()
        args, target, size = bridge.load_job()
        self.assertIn("127.0.0.1", args)
        self.assertIn("ssh", args)
        self.assertEqual(target, "ssh_2222")
        self.assertEqual(size, self.candidates.stat().st_size)

    def test_remote_host_rejected(self):
        self.write_config(host="192.0.2.10")
        with self.assertRaises(ValueError):
            bridge.load_job()

    def test_unknown_key_and_injection_rejected(self):
        self.write_config(extra="value")
        with self.assertRaises(ValueError):
            bridge.load_job()
        self.write_config(login="user;touch /tmp/x")
        with self.assertRaises(ValueError):
            bridge.load_job()

    def test_candidate_size_bound(self):
        self.write_config()
        with self.candidates.open("wb") as candidate_file:
            candidate_file.seek(16 * 1024 * 1024)
            candidate_file.write(b"x")
        with self.assertRaises(ValueError):
            bridge.load_job()

    def test_protocol_only_accepts_lab(self):
        instance = bridge.Bridge.__new__(bridge.Bridge)
        sent = []
        instance.send = sent.append
        instance.send_info = lambda: sent.append("info")
        instance.send_status = lambda: sent.append("status")
        instance.start = lambda: sent.append("started")
        instance.cancel = lambda: sent.append("cancelled")
        instance.handle("HYD1 RUN LAB")
        self.assertEqual(sent, ["started"])
        for line in ("HYD1 RUN SSH", "HYD1 RUN LAB extra", "RUN LAB", "HYD1 RUN ;id"):
            sent.clear()
            instance.handle(line)
            self.assertEqual(sent, ["HYD1 ERROR invalid_command"])


if __name__ == "__main__":
    unittest.main()
