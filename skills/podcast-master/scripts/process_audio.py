#!/usr/bin/env python3
"""Autonomous podcast mastering runner for the podcast-master skill.

One command takes a local audio file and returns a verified
``<stem>_processed.mp3`` beside it. The restoration policy lives in the C++
renderer; this runner owns decoding, delivery normalisation, MP3 encoding,
independent validation and atomic publication.

Exit codes:
    0  verified success
    1  I/O, decode or engine failure
    2  invalid request
    3  delivery/quality gate failure
    4  missing or incompatible dependency
    5  destination collision
  130  cancellation
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1
POLICY_VERSION = "1"
MIN_RENDERER = (0, 2, 0)

EXIT_OK = 0
EXIT_PROCESSING = 1
EXIT_INVALID = 2
EXIT_QUALITY = 3
EXIT_DEPENDENCY = 4
EXIT_COLLISION = 5
EXIT_CANCELLED = 130

PROFILES: dict[str, dict[str, Any]] = {
    "podcast": {
        "name": "podcast",
        "target_lufs": -16.0,
        "tolerance_lu": 1.0,
        "max_true_peak_dbtp": -1.0,
        "lra": 11.0,
        "mp3_bitrate_kbps": {1: 128, 2: 256},
    },
    "speech-mono": {
        "name": "speech-mono",
        "target_lufs": -19.0,
        "tolerance_lu": 1.0,
        "max_true_peak_dbtp": -1.0,
        "lra": 11.0,
        "mp3_bitrate_kbps": {1: 128, 2: 256},
    },
}

PCM_HEADROOM_DB = -2.0
MAX_ENCODE_ATTEMPTS = 3


class Failure(Exception):
    def __init__(self, code: int, status: str, message: str, detail: str | None = None):
        super().__init__(message)
        self.code = code
        self.status = status
        self.message = message
        self.detail = detail


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(argv: list[str], *, capture: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        argv,
        check=False,
        capture_output=capture,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def require_tool(name: str, explicit: str | None) -> str:
    if explicit:
        candidate = Path(explicit)
        if candidate.is_file():
            return str(candidate)
        found = shutil.which(explicit)
        if found:
            return found
        raise Failure(EXIT_DEPENDENCY, "dependency_error", f"{name} not found: {explicit}")
    found = shutil.which(name)
    if not found:
        raise Failure(EXIT_DEPENDENCY, "dependency_error", f"{name} is not on PATH")
    return found


def runtime_config_candidates() -> list[Path]:
    out = [Path(__file__).resolve().parents[1] / "runtime.json"]
    if os.environ.get("PODCAST_MASTER_CONFIG"):
        out.append(Path(os.environ["PODCAST_MASTER_CONFIG"]))
    xdg = os.environ.get("XDG_CONFIG_HOME")
    if xdg:
        out.append(Path(xdg) / "opencode" / "podcast-master" / "runtime.json")
    home = Path.home()
    out.append(home / ".config" / "opencode" / "podcast-master" / "runtime.json")
    return out


def configured_tool(name: str, explicit: str | None) -> str:
    if explicit:
        return require_tool(name, explicit)
    for config in runtime_config_candidates():
        try:
            data = json.loads(config.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if data.get(name):
            return require_tool(name, data[name])
    return require_tool(name, None)


def resolve_renderer(explicit: str | None) -> str:
    if explicit:
        return require_tool("podcast-sanity-render", explicit)
    env = os.environ.get("PODCAST_MASTER_RENDERER")
    if env:
        return require_tool("podcast-sanity-render", env)
    for config in runtime_config_candidates():
        try:
            data = json.loads(config.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        candidate = data.get("renderer")
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return require_tool("podcast-sanity-render", None)


def probe(ffprobe: str, path: Path) -> dict[str, Any]:
    result = run(
        [
            ffprobe,
            "-v",
            "error",
            "-print_format",
            "json",
            "-show_streams",
            "-show_format",
            str(path),
        ]
    )
    if result.returncode != 0:
        raise Failure(
            EXIT_INVALID,
            "invalid_request",
            "ffprobe could not read the input",
            result.stderr.strip(),
        )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise Failure(EXIT_INVALID, "invalid_request", "ffprobe returned invalid JSON", str(exc))


def select_audio_stream(info: dict[str, Any]) -> dict[str, Any]:
    streams = [s for s in info.get("streams", []) if s.get("codec_type") == "audio"]
    if not streams:
        raise Failure(EXIT_INVALID, "invalid_request", "the input has no audio stream")
    if len(streams) > 1:
        raise Failure(
            EXIT_INVALID,
            "invalid_request",
            "the input has multiple audio streams; select one before processing",
        )
    stream = streams[0]
    channels = int(stream.get("channels", 0) or 0)
    if channels not in (1, 2):
        raise Failure(
            EXIT_INVALID,
            "invalid_request",
            f"unsupported channel count: {channels} (mono or stereo only)",
        )
    return stream


def output_rate(stream: dict[str, Any]) -> int:
    rate = int(stream.get("sample_rate", 0) or 0)
    return rate if rate in (44100, 48000) else 48000


def decode_to_wav(ffmpeg: str, source: Path, stream: dict[str, Any], destination: Path) -> None:
    rate = output_rate(stream)
    argv = [
        ffmpeg,
        "-v",
        "error",
        "-nostdin",
        "-y",
        "-i",
        str(source),
        "-map",
        f"0:{stream.get('index', 0)}",
        "-vn",
        "-sn",
        "-dn",
        "-ar",
        str(rate),
        "-c:a",
        "pcm_f32le",
        str(destination),
    ]
    result = run(argv)
    if result.returncode != 0 or not destination.is_file() or destination.stat().st_size == 0:
        raise Failure(
            EXIT_PROCESSING,
            "processing_error",
            "the input could not be decoded",
            result.stderr.strip(),
        )


def renderer_capabilities(renderer: str) -> dict[str, Any]:
    result = run([renderer, "--capabilities", "--print-json"])
    if result.returncode != 0:
        raise Failure(
            EXIT_DEPENDENCY,
            "dependency_error",
            "the renderer did not answer --capabilities",
            result.stderr.strip(),
        )
    try:
        caps = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise Failure(EXIT_DEPENDENCY, "dependency_error", "invalid renderer capabilities", str(exc))
    return caps


def check_renderer(caps: dict[str, Any]) -> None:
    version = str(caps.get("version", "0.0.0"))
    try:
        parts = tuple(int(p) for p in version.split(".")[:3])
    except ValueError:
        parts = (0, 0, 0)
    if parts < MIN_RENDERER:
        raise Failure(
            EXIT_DEPENDENCY,
            "dependency_error",
            f"renderer {version} is older than the required {'.'.join(map(str, MIN_RENDERER))}",
        )
    if not caps.get("auto_clean"):
        raise Failure(EXIT_DEPENDENCY, "dependency_error", "renderer has no --auto-clean")
    if not caps.get("codecs", {}).get("mp3"):
        raise Failure(EXIT_DEPENDENCY, "dependency_error", "renderer build has no MP3 encoder")
    if int(caps.get("max_channels", 0)) < 2:
        raise Failure(EXIT_DEPENDENCY, "dependency_error", "renderer cannot preserve stereo")


def run_renderer(renderer: str, decoded: Path, work: Path) -> tuple[Path, dict[str, Any]]:
    cleaned = work / "cleaned.wav"
    engine_report = work / "engine.json"
    result = run(
        [
            renderer,
            str(decoded),
            "--auto-clean",
            "--out",
            str(cleaned),
            "--report",
            str(engine_report),
            "--quiet",
        ]
    )
    if result.returncode != 0:
        raise Failure(
            EXIT_QUALITY if result.returncode == 3 else EXIT_PROCESSING,
            "quality_gate_failed" if result.returncode == 3 else "processing_error",
            "the restoration engine failed",
            result.stderr.strip(),
        )
    if not cleaned.is_file() or cleaned.stat().st_size == 0:
        raise Failure(EXIT_PROCESSING, "processing_error", "the engine produced no audio")
    try:
        engine = json.loads(engine_report.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise Failure(EXIT_PROCESSING, "processing_error", "the engine report is unreadable", str(exc))
    return cleaned, engine


LOUDNORM_JSON = re.compile(r"\{[^{}]*\"input_i\"[^{}]*\}", re.DOTALL)


def loudnorm_measure(ffmpeg: str, source: Path, profile: dict[str, Any]) -> dict[str, str]:
    result = run(
        [
            ffmpeg,
            "-v",
            "info",
            "-nostdin",
            "-i",
            str(source),
            "-af",
            f"loudnorm=I={profile['target_lufs']}:TP={PCM_HEADROOM_DB}:"
            f"LRA={profile['lra']}:print_format=json",
            "-f",
            "null",
            "-",
        ]
    )
    if result.returncode != 0:
        raise Failure(
            EXIT_PROCESSING,
            "processing_error",
            "loudness measurement failed",
            result.stderr.strip(),
        )
    match = LOUDNORM_JSON.search(result.stderr)
    if not match:
        raise Failure(EXIT_PROCESSING, "processing_error", "loudnorm returned no measurements")
    try:
        measured = json.loads(match.group(0))
        if not all(math.isfinite(float(measured[key]))
                   for key in ("input_i", "input_tp", "input_lra", "input_thresh")):
            raise Failure(EXIT_QUALITY, "quality_gate_failed",
                          "integrated loudness is undefined for silent or too-short audio")
        return measured
    except json.JSONDecodeError as exc:
        raise Failure(EXIT_PROCESSING, "processing_error", "invalid loudnorm measurements", str(exc))


def loudnorm_apply(
    ffmpeg: str,
    source: Path,
    destination: Path,
    measured: dict[str, str],
    target_lufs: float,
    ceiling_db: float,
    rate: int,
) -> dict[str, Any]:
    chain = (
        f"loudnorm=I={target_lufs}:TP={ceiling_db}:LRA=11:"
        f"measured_I={measured['input_i']}:measured_TP={measured['input_tp']}:"
        f"measured_LRA={measured['input_lra']}:measured_thresh={measured['input_thresh']}:"
        f"offset={measured.get('target_offset', '0.0')}:linear=true:print_format=json"
    )
    result = run(
        [
            ffmpeg,
            "-v",
            "info",
            "-nostdin",
            "-y",
            "-i",
            str(source),
            "-af",
            chain,
            "-ar",
            str(rate),
            "-c:a",
            "pcm_f32le",
            str(destination),
        ]
    )
    if result.returncode != 0 or not destination.is_file():
        raise Failure(
            EXIT_PROCESSING,
            "processing_error",
            "delivery normalisation failed",
            result.stderr.strip(),
        )
    info: dict[str, Any] = {"normalization_type": "unknown"}
    match = LOUDNORM_JSON.search(result.stderr)
    if match:
        try:
            info.update(json.loads(match.group(0)))
        except json.JSONDecodeError:
            pass
    return info


def encode_mp3(ffmpeg: str, source: Path, destination: Path, rate: int, channels: int, kbps: int) -> None:
    result = run(
        [
            ffmpeg,
            "-v",
            "error",
            "-nostdin",
            "-y",
            "-i",
            str(source),
            "-c:a",
            "libmp3lame",
            "-b:a",
            f"{kbps}k",
            "-ar",
            str(rate),
            "-ac",
            str(channels),
            "-id3v2_version",
            "3",
            str(destination),
        ]
    )
    if result.returncode != 0 or not destination.is_file():
        raise Failure(
            EXIT_PROCESSING,
            "processing_error",
            "MP3 encoding failed",
            result.stderr.strip(),
        )


EBUR128_I = re.compile(r"I:\s+(-?\d+(?:\.\d+)?)\s+LUFS")
EBUR128_LRA = re.compile(r"LRA:\s+(-?\d+(?:\.\d+)?)\s+LU")
EBUR128_PEAK = re.compile(r"Peak:\s+(-?\d+(?:\.\d+)?)\s+dBFS")


def measure_delivery(ffmpeg: str, ffprobe: str, path: Path) -> dict[str, Any]:
    decode = run([ffmpeg, "-v", "error", "-nostdin", "-xerror", "-i", str(path), "-f", "null", "-"])
    if decode.returncode != 0:
        raise Failure(
            EXIT_QUALITY,
            "quality_gate_failed",
            "the encoded MP3 does not decode cleanly",
            decode.stderr.strip(),
        )
    meter = run(
        [
            ffmpeg,
            "-v",
            "info",
            "-nostdin",
            "-i",
            str(path),
            "-af",
            "ebur128=peak=true",
            "-f",
            "null",
            "-",
        ]
    )
    if meter.returncode != 0:
        raise Failure(
            EXIT_QUALITY,
            "quality_gate_failed",
            "the encoded MP3 could not be metered",
            meter.stderr.strip(),
        )
    text = meter.stderr
    i_match = EBUR128_I.findall(text)
    lra_match = EBUR128_LRA.findall(text)
    peak_match = EBUR128_PEAK.findall(text)
    if not i_match or not peak_match:
        raise Failure(
            EXIT_QUALITY,
            "quality_gate_failed",
            "integrated loudness or true peak is undefined for this file",
        )
    probe_info = probe(ffprobe, path)
    stream = probe_info["streams"][0]
    return {
        "integrated_lufs": float(i_match[-1]),
        "loudness_range_lu": float(lra_match[-1]) if lra_match else None,
        "true_peak_dbtp": float(peak_match[-1]),
        "codec": stream.get("codec_name"),
        "channels": int(stream.get("channels", 0) or 0),
        "sample_rate": int(stream.get("sample_rate", 0) or 0),
        "bit_rate_kbps": round(int(stream.get("bit_rate", 0) or 0) / 1000.0, 1),
        "duration_s": float(probe_info.get("format", {}).get("duration", 0.0) or 0.0),
    }


def check_free_space(work: Path, needed: int) -> None:
    free = shutil.disk_usage(work).free
    if free < needed:
        raise Failure(
            EXIT_PROCESSING,
            "processing_error",
            f"not enough free disk space: need {needed // (1024 * 1024)} MiB",
        )


def cache_dir() -> Path:
    if sys.platform.startswith("win"):
        base = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
    elif sys.platform == "darwin":
        base = str(Path.home() / "Library" / "Caches")
    else:
        base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base) / "podcast-master"


class DestinationLock:
    def __init__(self, destination: Path):
        self.path = destination.with_name(destination.name + ".lock")
        self.acquired = False

    def __enter__(self) -> "DestinationLock":
        try:
            fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            raise Failure(
                EXIT_COLLISION,
                "collision",
                f"destination lock exists: {self.path.name}",
                "Do not remove a lock until its owning job is confirmed stopped.",
            )
        os.write(fd, str(os.getpid()).encode("ascii"))
        os.close(fd)
        self.acquired = True
        return self

    def __exit__(self, *_: Any) -> None:
        if self.acquired:
            try:
                self.path.unlink()
            except OSError:
                pass


def publish(staging: Path, destination: Path, overwrite: bool) -> None:
    # os.replace is the atomic form on both platforms; it is used only when the
    # caller explicitly asked to replace an existing file.
    if overwrite:
        os.replace(staging, destination)
        return
    if destination.exists():
        raise Failure(
            EXIT_COLLISION,
            "collision",
            f"{destination.name} already exists",
            "pass --overwrite to replace it",
        )
    try:
        os.link(staging, destination)
        os.unlink(staging)
        return
    except FileExistsError:
        raise Failure(EXIT_COLLISION, "collision", f"{destination.name} already exists")
    except OSError as exc:
        raise Failure(EXIT_PROCESSING, "processing_error",
                      "atomic no-clobber publication is unavailable on this filesystem", str(exc))


def doctor(renderer: str | None, ffmpeg_arg: str | None, ffprobe_arg: str | None) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    ok = True

    def add(name: str, passed: bool, detail: str) -> None:
        nonlocal ok
        ok = ok and passed
        checks.append({"name": name, "ok": passed, "detail": detail})

    add("python", sys.version_info >= (3, 10), f"{sys.version.split()[0]} (need >= 3.10)")
    try:
        ffmpeg = configured_tool("ffmpeg", ffmpeg_arg)
        add("ffmpeg", True, ffmpeg)
        encoders = run([ffmpeg, "-hide_banner", "-encoders"])
        add("ffmpeg_lame", encoders.returncode == 0 and "libmp3lame" in encoders.stdout,
            "FFmpeg must provide libmp3lame")
    except Failure as exc:
        add("ffmpeg", False, exc.message)
    try:
        ffprobe = configured_tool("ffprobe", ffprobe_arg)
        add("ffprobe", True, ffprobe)
    except Failure as exc:
        add("ffprobe", False, exc.message)
    try:
        resolved = resolve_renderer(renderer)
        caps = renderer_capabilities(resolved)
        add("renderer", True, f"{resolved} (version {caps.get('version')})")
        try:
            check_renderer(caps)
            add("renderer_contract", True, "auto-clean, stereo and MP3 available")
        except Failure as exc:
            add("renderer_contract", False, exc.message)
        add("mp3_encoder", bool(caps.get("codecs", {}).get("mp3")), "libmp3lame")
    except Failure as exc:
        add("renderer", False, exc.message)
    return {
        "schema_version": SCHEMA_VERSION,
        "ok": ok,
        "status": "ok" if ok else "dependency_error",
        "checks": checks,
    }


def process(args: argparse.Namespace) -> dict[str, Any]:
    started = time.time()
    source = Path(args.input).expanduser().resolve()
    if not source.is_file():
        raise Failure(EXIT_INVALID, "invalid_request", f"input is not a file: {source}")
    if source.stat().st_size == 0:
        raise Failure(EXIT_INVALID, "invalid_request", "input is empty")

    profile = PROFILES[args.profile]
    ffmpeg = configured_tool("ffmpeg", args.ffmpeg)
    ffprobe = configured_tool("ffprobe", args.ffprobe)
    renderer = resolve_renderer(args.renderer)
    caps = renderer_capabilities(renderer)
    check_renderer(caps)

    info = probe(ffprobe, source)
    stream = select_audio_stream(info)
    channels = int(stream["channels"])
    rate = output_rate(stream)
    duration = float(info.get("format", {}).get("duration", 0.0) or 0.0)

    destination = source.with_name(source.stem + "_processed.mp3")
    report_target = Path(args.report).expanduser().resolve() if args.report else None
    if report_target:
        for protected in (source, destination):
            if report_target == protected or (report_target.exists() and protected.exists()
                                             and os.path.samefile(report_target, protected)):
                raise Failure(EXIT_INVALID, "invalid_request",
                              "report path aliases the source or destination audio")
    if destination.resolve() == source.resolve():
        raise Failure(EXIT_INVALID, "invalid_request", "destination would overwrite the source")
    if destination.exists() and not args.overwrite:
        raise Failure(
            EXIT_COLLISION,
            "collision",
            f"{destination.name} already exists",
            "pass --overwrite to replace it",
        )

    source_hash = sha256_file(source)
    cache = cache_dir()
    cache.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="podcast-master-", dir=str(cache)))
    try:
        if duration > 0:
            check_free_space(work, int(duration * rate * channels * 4 * 4) + 64 * 1024 * 1024)
        decoded = work / "decoded.wav"
        decode_to_wav(ffmpeg, source, stream, decoded)
        decoded_bytes = decoded.stat().st_size
        check_free_space(work, decoded_bytes * 3 + 64 * 1024 * 1024)

        cleaned, engine = run_renderer(renderer, decoded, work)
        if engine.get("analysis", {}).get("channels") not in (None, channels):
            log("warning: engine channel count differs from the decoded input")

        kbps = profile["mp3_bitrate_kbps"][channels]
        target_lufs = profile["target_lufs"]
        ceiling = PCM_HEADROOM_DB
        attempts: list[dict[str, Any]] = []
        final_measurement: dict[str, Any] | None = None
        staging: Path | None = None

        for attempt in range(1, MAX_ENCODE_ATTEMPTS + 1):
            measured = loudnorm_measure(ffmpeg, cleaned, profile)
            normalized = work / f"normalized-{attempt}.wav"
            norm_info = loudnorm_apply(
                ffmpeg, cleaned, normalized, measured, target_lufs, ceiling, rate
            )
            encoded = work / f"encoded-{attempt}.mp3"
            encode_mp3(ffmpeg, normalized, encoded, rate, channels, kbps)
            delivery = measure_delivery(ffmpeg, ffprobe, encoded)
            attempts.append(
                {
                    "attempt": attempt,
                    "pcm_ceiling_dbtp": ceiling,
                    "target_lufs": target_lufs,
                    "normalization_type": norm_info.get("normalization_type"),
                    "measured": delivery,
                }
            )
            final_measurement = delivery
            loudness_error = delivery["integrated_lufs"] - profile["target_lufs"]
            peak_excess = delivery["true_peak_dbtp"] - profile["max_true_peak_dbtp"]
            if abs(loudness_error) <= profile["tolerance_lu"] and peak_excess <= 0.0:
                staging = encoded
                break
            if peak_excess > 0.0:
                ceiling -= peak_excess + 0.2
            if abs(loudness_error) > profile["tolerance_lu"]:
                target_lufs -= loudness_error
            log(
                f"attempt {attempt}: I {delivery['integrated_lufs']:.2f} LUFS, "
                f"TP {delivery['true_peak_dbtp']:.2f} dBTP; retrying"
            )

        if staging is None or final_measurement is None:
            raise Failure(
                EXIT_QUALITY,
                "quality_gate_failed",
                "the encoded file never met the delivery targets",
                json.dumps(attempts, indent=2),
            )

        duration_ok = duration <= 0.0 or abs(final_measurement["duration_s"] - duration) <= 0.1
        codec_ok = final_measurement["codec"] == "mp3"
        channels_ok = final_measurement["channels"] == channels
        loudness_ok = (
            abs(final_measurement["integrated_lufs"] - profile["target_lufs"])
            <= profile["tolerance_lu"]
        )
        peak_ok = final_measurement["true_peak_dbtp"] <= profile["max_true_peak_dbtp"]
        conformance = {
            "loudness_ok": loudness_ok,
            "true_peak_ok": peak_ok,
            "duration_ok": duration_ok,
            "codec_ok": codec_ok and channels_ok,
            "ok": loudness_ok and peak_ok and duration_ok and codec_ok and channels_ok,
        }
        if not conformance["ok"]:
            raise Failure(
                EXIT_QUALITY,
                "quality_gate_failed",
                "the encoded file failed a delivery gate",
                json.dumps(conformance, indent=2),
            )

        if sha256_file(source) != source_hash:
            raise Failure(
                EXIT_PROCESSING,
                "processing_error",
                "the source file changed during processing",
            )

        fd, stage_name = tempfile.mkstemp(prefix=f".{destination.name}.", dir=destination.parent)
        os.close(fd)
        staged = Path(stage_name)
        try:
            shutil.copyfile(staging, staged)
            with DestinationLock(destination):
                publish(staged, destination, args.overwrite)
        finally:
            staged.unlink(missing_ok=True)

        result: dict[str, Any] = {
            "schema_version": SCHEMA_VERSION,
            "ok": True,
            "exit_code": EXIT_OK,
            "status": "success",
            "input": {
                "path": str(source),
                "sha256": source_hash,
                "codec": stream.get("codec_name"),
                "channels": channels,
                "sample_rate": int(stream.get("sample_rate", 0) or 0),
                "duration_s": duration,
                "stream_index": stream.get("index"),
            },
            "output": {
                "path": str(destination),
                "sha256": sha256_file(destination),
                **final_measurement,
            },
            "profile": {
                "name": profile["name"],
                "target_lufs": profile["target_lufs"],
                "tolerance_lu": profile["tolerance_lu"],
                "max_true_peak_dbtp": profile["max_true_peak_dbtp"],
                "mp3_bitrate_kbps": kbps,
            },
            "measurements": {
                "integrated_lufs": final_measurement["integrated_lufs"],
                "true_peak_dbtp": final_measurement["true_peak_dbtp"],
                "loudness_range_lu": final_measurement["loudness_range_lu"],
            },
            "conformance": conformance,
            "engine": engine,
            "attempts": attempts,
            "warnings": engine.get("warnings", []),
            "error": None,
            "versions": {
                "runner": "1.0.0",
                "schema": SCHEMA_VERSION,
                "policy": POLICY_VERSION,
                "renderer": caps.get("version"),
                "ffmpeg": ffmpeg_version(ffmpeg),
            },
            "timing": {"elapsed_s": round(time.time() - started, 3)},
        }

        report_text = json.dumps(result, indent=2)
        if report_target:
            report_target.parent.mkdir(parents=True, exist_ok=True)
            report_target.write_text(report_text + "\n", encoding="utf-8")
        else:
            (cache / "last.json").write_text(report_text + "\n", encoding="utf-8")
        return result
    finally:
        shutil.rmtree(work, ignore_errors=True)


def ffmpeg_version(ffmpeg: str) -> str:
    result = run([ffmpeg, "-version"])
    first = result.stdout.splitlines()[0] if result.stdout else ""
    match = re.search(r"ffmpeg version (\S+)", first)
    return match.group(1) if match else "unknown"


def failure_result(exc: Failure, started: float) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "ok": False,
        "exit_code": exc.code,
        "status": exc.status,
        "error": {"message": exc.message, "detail": exc.detail},
        "timing": {"elapsed_s": round(time.time() - started, 3)},
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Autonomous podcast mastering")
    parser.add_argument("--input", help="local audio file to process")
    parser.add_argument("--json", action="store_true", help="print one JSON result on stdout")
    parser.add_argument("--doctor", action="store_true", help="check runtime dependencies")
    parser.add_argument("--renderer", help="path to podcast-sanity-render")
    parser.add_argument("--ffmpeg", help="path to ffmpeg")
    parser.add_argument("--ffprobe", help="path to ffprobe")
    parser.add_argument("--overwrite", action="store_true", help="replace an existing output")
    parser.add_argument("--report", help="write the JSON report to this path")
    parser.add_argument("--profile", default="podcast", choices=sorted(PROFILES))
    return parser


def main(argv: list[str] | None = None) -> int:
    started = time.time()
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.doctor:
        result = doctor(args.renderer, args.ffmpeg, args.ffprobe)
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            for check in result["checks"]:
                mark = "ok" if check["ok"] else "FAIL"
                print(f"[{mark}] {check['name']}: {check['detail']}")
        return EXIT_OK if result["ok"] else EXIT_DEPENDENCY

    if not args.input:
        parser.error("--input is required unless --doctor is used")

    try:
        result = process(args)
        code = EXIT_OK
    except Failure as exc:
        result = failure_result(exc, started)
        code = exc.code
    except KeyboardInterrupt:
        result = {
            "schema_version": SCHEMA_VERSION,
            "ok": False,
            "exit_code": EXIT_CANCELLED,
            "status": "cancelled",
            "error": {"message": "cancelled by the user", "detail": None},
        }
        code = EXIT_CANCELLED
    except Exception as exc:  # noqa: BLE001 - one JSON contract on stdout
        result = {
            "schema_version": SCHEMA_VERSION,
            "ok": False,
            "exit_code": EXIT_PROCESSING,
            "status": "processing_error",
            "error": {"message": str(exc), "detail": type(exc).__name__},
        }
        code = EXIT_PROCESSING

    if args.json:
        print(json.dumps(result, indent=2))
    elif code == EXIT_OK:
        out = result.get("output", {})
        print(
            f"{out.get('path')}  {out.get('duration_s', 0):.2f} s  "
            f"{out.get('integrated_lufs', 0):.1f} LUFS  "
            f"{out.get('true_peak_dbtp', 0):.1f} dBTP"
        )
    else:
        error = result.get("error") or {}
        print(f"{result.get('status')}: {error.get('message')}", file=sys.stderr)
    return code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(EXIT_CANCELLED)
