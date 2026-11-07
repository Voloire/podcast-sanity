"""Fast failure-path tests, without audio processing or global installation."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load("runner", "skills/podcast-master/scripts/process_audio.py")
installer = load("installer", "tools/install_opencode_skill.py")


class SafetyTests(unittest.TestCase):
    def test_publication_never_falls_back_to_racy_replace(self):
        with tempfile.TemporaryDirectory() as temp:
            stage, dest = Path(temp) / "stage", Path(temp) / "out.mp3"
            stage.write_bytes(b"validated")
            with patch.object(runner.os, "link", side_effect=OSError("unsupported")):
                with self.assertRaises(runner.Failure):
                    runner.publish(stage, dest, False)
            self.assertFalse(dest.exists())

    def test_publication_collision_preserves_previous_file(self):
        with tempfile.TemporaryDirectory() as temp:
            stage, dest = Path(temp) / "stage", Path(temp) / "out.mp3"
            stage.write_bytes(b"new")
            dest.write_bytes(b"original")
            with self.assertRaises(runner.Failure):
                runner.publish(stage, dest, False)
            self.assertEqual(dest.read_bytes(), b"original")

    def test_old_lock_is_not_stolen(self):
        with tempfile.TemporaryDirectory() as temp:
            destination = Path(temp) / "out.mp3"
            lock = runner.DestinationLock(destination)
            lock.path.write_text("123")
            runner.os.utime(lock.path, (0, 0))
            with self.assertRaises(runner.Failure):
                with lock:
                    pass
            self.assertEqual(lock.path.read_text(), "123")

    def test_installer_uninstall_refusal_is_transactional(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = installer.install_dir(root)
            installer.copy_skill(installer.SOURCE_SKILL, target, {}, root)
            (target / "SKILL.md").write_text("local edits")
            before = {str(p): p.read_bytes() for p in target.rglob("*") if p.is_file()}
            args = installer.build_parser().parse_args(["--action", "uninstall"])
            with self.assertRaises(installer.InstallerError):
                installer.action_uninstall(root, args)
            after = {str(p): p.read_bytes() for p in target.rglob("*") if p.is_file()}
            self.assertEqual(before, after)

    def test_installer_removes_manifest_and_runtime_on_uninstall(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = installer.install_dir(root)
            installer.copy_skill(installer.SOURCE_SKILL, target, {}, root)
            args = installer.build_parser().parse_args(["--action", "uninstall"])
            installer.action_uninstall(root, args)
            self.assertFalse(target.exists())

    def test_runtime_resolves_all_tools_from_config(self):
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / "runtime.json"
            executable = Path(temp) / "ffmpeg"
            executable.touch()
            config.write_text(json.dumps({"ffmpeg": str(executable)}))
            with patch.object(runner, "runtime_config_candidates", return_value=[config]):
                self.assertEqual(runner.configured_tool("ffmpeg", None), str(executable))


if __name__ == "__main__":
    unittest.main(verbosity=2)
