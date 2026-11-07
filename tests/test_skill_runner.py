#!/usr/bin/env python3
"""Contract tests for the podcast-master runner.

They drive the real runner against the real renderer and the real FFmpeg, so
they prove the deliverable a user gets: a correctly named MP3, at the measured
targets, with the source untouched. Run directly, or through CTest:

    PODCAST_TEST_RENDERER=build/podcast-sanity-render python3 tests/test_skill_runner.py
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RUNNER = REPO / "skills" / "podcast-master" / "scripts" / "process_audio.py"


def renderer_path() -> Path:
    env = os.environ.get("PODCAST_TEST_RENDERER")
    if env:
        return Path(env).resolve()
    for name in ("build-skill", "build"):
        candidate = REPO / name / "podcast-sanity-render"
        if candidate.is_file():
            return candidate
        candidate = REPO / name / "podcast-sanity-render.exe"
        if candidate.is_file():
            return candidate
    raise RuntimeError("renderer not found; set PODCAST_TEST_RENDERER")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_runner(*args: str) -> tuple[int, dict]:
    result = subprocess.run(
        [sys.executable, str(RUNNER), *args, "--json"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:  # pragma: no cover - contract failure
        raise AssertionError(
            f"stdout is not one JSON object ({exc}): {result.stdout!r} / {result.stderr!r}"
        )
    return result.returncode, payload


def probe_channels(path: Path) -> tuple[int, int, str]:
    result = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-print_format",
            "json",
            "-show_streams",
            str(path),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    stream = json.loads(result.stdout)["streams"][0]
    return int(stream["channels"]), int(stream["sample_rate"]), stream["codec_name"]


def read_stereo(path: Path) -> tuple[list[float], list[float]]:
    raw = subprocess.check_output(
        ["ffmpeg", "-v", "error", "-i", str(path), "-f", "f32le", "-"]
    )
    values = struct.unpack("<%df" % (len(raw) // 4), raw)
    return values[0::2], values[1::2]


class RunnerContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.renderer = renderer_path()
        cls.tmp = Path(tempfile.mkdtemp(prefix="podcast-runner-test-"))
        cls.mono = cls.tmp / "Episode 07.take2.wav"
        subprocess.run(
            [
                str(cls.renderer),
                "--gen-corpus",
                str(cls.mono),
                "--profile",
                "home",
                "--seconds",
                "6",
                "--quiet",
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def copy_source(self, name: str) -> Path:
        destination = self.tmp / name
        shutil.copyfile(self.mono, destination)
        return destination

    def test_doctor_reports_a_working_runtime(self) -> None:
        code, payload = run_runner("--doctor", "--renderer", str(self.renderer))
        self.assertEqual(code, 0, payload)
        self.assertTrue(payload["ok"])
        names = {check["name"] for check in payload["checks"]}
        self.assertIn("renderer_contract", names)
        self.assertIn("mp3_encoder", names)

    def test_one_request_produces_a_verified_mp3(self) -> None:
        source = self.copy_source("interview one.wav")
        before = sha256(source)
        code, payload = run_runner("--input", str(source), "--renderer", str(self.renderer))
        self.assertEqual(code, 0, payload)
        self.assertTrue(payload["ok"])

        output = Path(payload["output"]["path"])
        self.assertEqual(output.name, "interview one_processed.mp3")
        self.assertEqual(output.parent, source.parent)
        self.assertEqual(sha256(source), before, "the source must never be modified")

        channels, rate, codec = probe_channels(output)
        self.assertEqual(codec, "mp3")
        self.assertEqual(channels, 1)
        self.assertIn(rate, (44100, 48000))
        self.assertEqual(payload["output"]["bit_rate_kbps"], 128.0)

        measurements = payload["measurements"]
        self.assertLessEqual(abs(measurements["integrated_lufs"] + 16.0), 1.0)
        self.assertLessEqual(measurements["true_peak_dbtp"], -1.0)
        self.assertTrue(payload["conformance"]["ok"])
        self.assertGreaterEqual(payload["output"]["duration_s"], 5.5)
        self.assertEqual(payload["engine"]["policy_version"], "1")
        self.assertEqual(len(payload["attempts"]), 1)

    def test_existing_destination_is_a_collision_not_an_overwrite(self) -> None:
        source = self.copy_source("second take.wav")
        first_code, first = run_runner("--input", str(source), "--renderer", str(self.renderer))
        self.assertEqual(first_code, 0, first)
        output = Path(first["output"]["path"])
        digest = sha256(output)

        second_code, second = run_runner("--input", str(source), "--renderer", str(self.renderer))
        self.assertEqual(second_code, 5, second)
        self.assertEqual(second["status"], "collision")
        self.assertEqual(sha256(output), digest, "a collision must not touch the existing file")
        self.assertFalse((source.parent / "second take_processed_1.mp3").exists())

        override_code, override = run_runner(
            "--input", str(source), "--renderer", str(self.renderer), "--overwrite"
        )
        self.assertEqual(override_code, 0, override)
        self.assertTrue(Path(override["output"]["path"]).is_file())

    def test_stereo_input_stays_stereo(self) -> None:
        stereo = self.tmp / "stereo source.wav"
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-y",
                "-i",
                str(self.mono),
                "-filter_complex",
                "[0:a]asplit=2[l][r];[r]adelay=3|3,volume=-2dB[rr];"
                "[l][rr]amerge=inputs=2[out]",
                "-map",
                "[out]",
                str(stereo),
            ],
            check=True,
        )
        code, payload = run_runner("--input", str(stereo), "--renderer", str(self.renderer))
        self.assertEqual(code, 0, payload)
        output = Path(payload["output"]["path"])
        channels, _, codec = probe_channels(output)
        self.assertEqual(codec, "mp3")
        self.assertEqual(channels, 2)
        self.assertEqual(payload["output"]["bit_rate_kbps"], 256.0)

        left, right = read_stereo(output)
        difference = sum(abs(a - b) for a, b in zip(left, right)) / max(1, len(left))
        self.assertGreater(difference, 1e-4, "the stereo image was collapsed to mono")
        self.assertLessEqual(payload["engine"]["full_file_check"]["stereo_imbalance_change_db"], 0.5)

    def test_invalid_inputs_fail_without_leaving_an_output(self) -> None:
        missing = self.tmp / "does not exist.wav"
        code, payload = run_runner("--input", str(missing), "--renderer", str(self.renderer))
        self.assertEqual(code, 2, payload)
        self.assertEqual(payload["status"], "invalid_request")

        empty = self.tmp / "empty.wav"
        empty.write_bytes(b"")
        code, payload = run_runner("--input", str(empty), "--renderer", str(self.renderer))
        self.assertEqual(code, 2, payload)
        self.assertFalse((self.tmp / "empty_processed.mp3").exists())

    def test_report_can_be_exported_and_matches_the_contract(self) -> None:
        source = self.copy_source("report case.wav")
        report = self.tmp / "reports" / "result.json"
        code, payload = run_runner(
            "--input",
            str(source),
            "--renderer",
            str(self.renderer),
            "--report",
            str(report),
        )
        self.assertEqual(code, 0, payload)
        self.assertTrue(report.is_file())
        stored = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(stored["schema_version"], 1)
        for key in ("input", "output", "profile", "measurements", "conformance", "engine"):
            self.assertIn(key, stored)
        self.assertEqual(stored["output"]["path"], payload["output"]["path"])

    def test_report_cannot_overwrite_the_source(self) -> None:
        source = self.copy_source("report alias.wav")
        before = sha256(source)
        code, payload = run_runner("--input", str(source), "--renderer", str(self.renderer),
                                   "--report", str(source))
        self.assertEqual(code, 2, payload)
        self.assertEqual(sha256(source), before)

    def test_silence_fails_without_publishing(self) -> None:
        import wave
        source = self.tmp / "silence.wav"
        with wave.open(str(source), "wb") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(2)
            handle.setframerate(48000)
            handle.writeframes(bytes(48000 * 2))
        code, payload = run_runner("--input", str(source), "--renderer", str(self.renderer))
        self.assertEqual(code, 3, payload)
        self.assertFalse(source.with_name("silence_processed.mp3").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
