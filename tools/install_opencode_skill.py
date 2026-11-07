#!/usr/bin/env python3
"""Install the podcast-master skill into the user's OpenCode skills directory.

The canonical skill lives in this repository under ``skills/podcast-master``.
This tool copies it, complete with scripts and references, into the global
OpenCode skills directory so it can be used from any project, and writes the
runtime configuration that tells the runner where the renderer, FFmpeg and
ffprobe actually are on this machine.

Nothing else is touched: no ``opencode.json`` edit, no Claude or Codex skill
tree, no installation of the desktop application.

Actions:
    check       report what is installed and whether the runtime is compatible
    install     copy the skill; refuses to replace an existing installation
    update      back up the current installation, then copy the new one
    uninstall   remove only the files recorded in the install manifest

Every action accepts ``--dry-run`` and ``--json``. Exit codes: 0 success,
1 failure, 2 invalid request, 4 dependency error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

SKILL_NAME = "podcast-master"
SKILL_VERSION = "1.0.0"
MANIFEST_NAME = ".install-manifest.json"
MIN_RENDERER = (0, 2, 0)

EXIT_OK = 0
EXIT_FAILURE = 1
EXIT_INVALID = 2
EXIT_DEPENDENCY = 4

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_SKILL = REPO_ROOT / "skills" / SKILL_NAME

REQUIRED_FILES = [
    "SKILL.md",
    "scripts/process_audio.py",
    "references/processing-policy.md",
    "references/report-schema.json",
]


class InstallerError(Exception):
    def __init__(self, code: int, message: str, detail: str | None = None):
        super().__init__(message)
        self.code = code
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


def config_root(explicit: str | None) -> Path:
    """The global OpenCode configuration root."""
    if explicit:
        return Path(explicit).expanduser().resolve()
    xdg = os.environ.get("XDG_CONFIG_HOME")
    if xdg:
        return Path(xdg).expanduser() / "opencode"
    return Path.home() / ".config" / "opencode"


def skills_dir(root: Path) -> Path:
    return root / "skills"


def install_dir(root: Path) -> Path:
    return skills_dir(root) / SKILL_NAME


def runtime_config_path(root: Path) -> Path:
    return root / SKILL_NAME / "runtime.json"


def collect_files(source: Path) -> list[Path]:
    files = []
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        if relative.name == MANIFEST_NAME or "__pycache__" in relative.parts:
            continue
        if relative.suffix == ".pyc":
            continue
        files.append(relative)
    return files


def verify_source() -> None:
    if not SOURCE_SKILL.is_dir():
        raise InstallerError(EXIT_INVALID, f"source skill not found: {SOURCE_SKILL}")
    missing = [name for name in REQUIRED_FILES if not (SOURCE_SKILL / name).is_file()]
    if missing:
        raise InstallerError(
            EXIT_INVALID,
            "source skill is incomplete",
            ", ".join(missing),
        )


def find_tool(name: str, explicit: str | None) -> str | None:
    if explicit:
        candidate = Path(explicit).expanduser()
        if candidate.is_file():
            return str(candidate.resolve())
        found = shutil.which(explicit)
        return found
    return shutil.which(name)


def renderer_capabilities(renderer: str) -> dict[str, Any]:
    result = subprocess.run(
        [renderer, "--capabilities", "--print-json"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise InstallerError(
            EXIT_DEPENDENCY,
            f"the renderer did not answer --capabilities: {renderer}",
            result.stderr.strip(),
        )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise InstallerError(EXIT_DEPENDENCY, "invalid renderer capabilities", str(exc))


def check_renderer(caps: dict[str, Any]) -> list[str]:
    problems = []
    version = str(caps.get("version", "0.0.0"))
    try:
        parts = tuple(int(p) for p in version.split(".")[:3])
    except ValueError:
        parts = (0, 0, 0)
    if parts < MIN_RENDERER:
        problems.append(
            f"renderer {version} is older than the required {'.'.join(map(str, MIN_RENDERER))}"
        )
    if not caps.get("auto_clean"):
        problems.append("renderer has no --auto-clean")
    if not caps.get("codecs", {}).get("mp3"):
        problems.append("renderer build has no MP3 encoder")
    if int(caps.get("max_channels", 0)) < 2:
        problems.append("renderer cannot preserve stereo")
    return problems


def resolve_runtime(args: argparse.Namespace) -> dict[str, Any]:
    try:
        saved = json.loads(runtime_config_path(config_root(args.config_root)).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        saved = {}
    renderer = find_tool("podcast-sanity-render", args.renderer or saved.get("renderer"))
    if not renderer:
        raise InstallerError(
            EXIT_DEPENDENCY,
            "podcast-sanity-render not found; pass --renderer with its full path",
        )
    caps = renderer_capabilities(renderer)
    problems = check_renderer(caps)
    if problems:
        raise InstallerError(EXIT_DEPENDENCY, "renderer is not compatible", "; ".join(problems))
    ffmpeg = find_tool("ffmpeg", args.ffmpeg or saved.get("ffmpeg"))
    ffprobe = find_tool("ffprobe", args.ffprobe or saved.get("ffprobe"))
    if not ffmpeg:
        raise InstallerError(EXIT_DEPENDENCY, "ffmpeg not found; pass --ffmpeg with its full path")
    if not ffprobe:
        raise InstallerError(EXIT_DEPENDENCY, "ffprobe not found; pass --ffprobe with its full path")
    return {
        "schema_version": 1,
        "skill": SKILL_NAME,
        "skill_version": SKILL_VERSION,
        "renderer": renderer,
        "renderer_version": caps.get("version"),
        "ffmpeg": ffmpeg,
        "ffprobe": ffprobe,
        "installed_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }


def read_manifest(target: Path) -> dict[str, Any] | None:
    path = target / MANIFEST_NAME
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
        if manifest.get("skill") != SKILL_NAME:
            return None
        for entry in manifest.get("files", []):
            relative = Path(entry["path"])
            if relative.is_absolute() or ".." in relative.parts or "\\" in entry["path"]:
                return None
            if (target / relative).is_symlink() or not (target / relative).resolve().is_relative_to(target.resolve()):
                return None
        return manifest
    except (OSError, json.JSONDecodeError):
        return None


def installed_state(root: Path) -> dict[str, Any]:
    target = install_dir(root)
    manifest = read_manifest(target)
    config = runtime_config_path(root)
    state: dict[str, Any] = {
        "install_dir": str(target),
        "installed": target.is_dir(),
        "manifest": manifest,
        "runtime_config": str(config),
        "runtime_configured": config.is_file(),
    }
    if manifest:
        recorded = {entry["path"]: entry["sha256"] for entry in manifest.get("files", [])}
        changed = []
        for relative, digest in recorded.items():
            path = target / relative
            if not path.is_file():
                changed.append({"path": relative, "state": "missing"})
            elif sha256_file(path) != digest:
                changed.append({"path": relative, "state": "modified"})
        state["modified_files"] = changed
        state["up_to_date"] = not changed and manifest.get("skill_version") == SKILL_VERSION
        for relative in collect_files(SOURCE_SKILL):
            if recorded.get(relative.as_posix()) != sha256_file(SOURCE_SKILL / relative):
                state["up_to_date"] = False
    return state


def copy_skill(source: Path, target: Path, runtime: dict[str, Any], root: Path) -> dict[str, Any]:
    target.mkdir(parents=True, exist_ok=True)
    files = []
    for relative in collect_files(source):
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / relative, destination)
        files.append({"path": relative.as_posix(), "sha256": sha256_file(destination)})
    manifest = {
        "schema_version": 1,
        "skill": SKILL_NAME,
        "skill_version": SKILL_VERSION,
        "installed_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source": str(source),
        "files": files,
    }
    (target / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    config = runtime_config_path(root)
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text(json.dumps(runtime, indent=2) + "\n", encoding="utf-8")
    # Keep explicit config roots usable from any cwd, without environment setup.
    (target / "runtime.json").write_text(json.dumps(runtime, indent=2) + "\n", encoding="utf-8")
    return manifest


def backup_install(target: Path, root: Path) -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    backup_root = root / "backups" / SKILL_NAME
    backup_root.mkdir(parents=True, exist_ok=True)
    destination = backup_root / stamp
    shutil.copytree(target, destination)
    return destination


def action_check(root: Path, args: argparse.Namespace) -> dict[str, Any]:
    verify_source()
    state = installed_state(root)
    problems = []
    if not state["installed"]:
        problems.append("skill is not installed")
    elif not state["manifest"]:
        problems.append("installation has no manifest; rerun with --action update")
    elif state.get("modified_files"):
        problems.append("installed files were modified or removed")
    elif state.get("up_to_date") is False:
        problems.append("installed copy is from a different skill version")
    runtime = None
    try:
        runtime = resolve_runtime(args)
    except InstallerError as exc:
        problems.append(exc.message)
    return {
        "ok": not problems,
        "action": "check",
        "state": state,
        "runtime": runtime,
        "problems": problems,
    }


def action_install(root: Path, args: argparse.Namespace, update: bool) -> dict[str, Any]:
    verify_source()
    target = install_dir(root)
    runtime = resolve_runtime(args)
    state = installed_state(root)
    backup = None
    if target.exists():
        if not update:
            raise InstallerError(
                EXIT_INVALID,
                f"{target} already exists; use --action update to replace it",
            )
        if not state.get("manifest"):
            raise InstallerError(
                EXIT_INVALID,
                f"{target} exists but was not installed by this tool; refusing to replace it",
            )
        known = {entry["path"] for entry in state["manifest"]["files"]}
        known.update({MANIFEST_NAME, "runtime.json"})
        extra = [p.relative_to(target).as_posix() for p in target.rglob("*")
                 if p.is_file() and p.relative_to(target).as_posix() not in known]
        if extra:
            raise InstallerError(EXIT_INVALID, "installation contains unowned files; refusing replacement",
                                 ", ".join(extra))
        if not args.dry_run:
            backup = backup_install(target, root)
            shutil.rmtree(target)
        else:
            backup = root / "backups" / SKILL_NAME / "<timestamp>"
    if args.dry_run:
        return {
            "ok": True,
            "action": "update" if update else "install",
            "dry_run": True,
            "install_dir": str(target),
            "runtime_config": str(runtime_config_path(root)),
            "runtime": runtime,
            "backup": str(backup) if backup else None,
            "files": [p.as_posix() for p in collect_files(SOURCE_SKILL)],
        }
    manifest = copy_skill(SOURCE_SKILL, target, runtime, root)
    return {
        "ok": True,
        "action": "update" if update else "install",
        "dry_run": False,
        "install_dir": str(target),
        "runtime_config": str(runtime_config_path(root)),
        "runtime": runtime,
        "backup": str(backup) if backup else None,
        "files": [entry["path"] for entry in manifest["files"]],
    }


def action_uninstall(root: Path, args: argparse.Namespace) -> dict[str, Any]:
    target = install_dir(root)
    state = installed_state(root)
    if not target.exists():
        return {"ok": True, "action": "uninstall", "removed": [], "note": "not installed"}
    manifest = state.get("manifest")
    if not manifest:
        raise InstallerError(
            EXIT_INVALID,
            f"{target} was not installed by this tool; refusing to delete it",
        )
    removed = []
    kept = []
    for entry in manifest.get("files", []):
        path = target / entry["path"]
        if not path.is_file():
            continue
        if sha256_file(path) != entry["sha256"]:
            kept.append(entry["path"])
        else:
            removed.append(entry["path"])
    if kept and not args.force:
        raise InstallerError(
            EXIT_INVALID,
            "some installed files were modified locally; rerun with --force to remove them",
            ", ".join(kept),
        )
    if args.force:
        removed.extend(kept)
        kept = []
    if not args.dry_run:
        for relative in removed:
            (target / relative).unlink(missing_ok=True)
        (target / MANIFEST_NAME).unlink(missing_ok=True)
        (target / "runtime.json").unlink(missing_ok=True)
        for path in sorted(target.rglob("*"), reverse=True):
            if path.is_dir():
                try:
                    path.rmdir()
                except OSError:
                    pass
        try:
            target.rmdir()
        except OSError:
            pass
        config = runtime_config_path(root)
        if config.is_file():
            config.unlink()
        try:
            config.parent.rmdir()
        except OSError:
            pass
    return {
        "ok": True,
        "action": "uninstall",
        "dry_run": args.dry_run,
        "install_dir": str(target),
        "removed": removed,
        "kept": kept,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Install the podcast-master OpenCode skill")
    parser.add_argument(
        "--action",
        default="install",
        choices=["check", "install", "update", "uninstall"],
    )
    parser.add_argument("--renderer", help="path to podcast-sanity-render")
    parser.add_argument("--ffmpeg", help="path to ffmpeg")
    parser.add_argument("--ffprobe", help="path to ffprobe")
    parser.add_argument("--config-root", help="OpenCode global config root (default ~/.config/opencode)")
    parser.add_argument("--dry-run", action="store_true", help="report without changing anything")
    parser.add_argument("--force", action="store_true", help="remove locally modified files on uninstall")
    parser.add_argument("--json", action="store_true", help="print one JSON object on stdout")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = config_root(args.config_root)
    try:
        if args.action == "check":
            result = action_check(root, args)
        elif args.action == "install":
            result = action_install(root, args, update=False)
        elif args.action == "update":
            result = action_install(root, args, update=True)
        else:
            result = action_uninstall(root, args)
        code = EXIT_OK if result.get("ok") else EXIT_FAILURE
    except InstallerError as exc:
        result = {
            "ok": False,
            "action": args.action,
            "error": {"message": exc.message, "detail": exc.detail},
        }
        code = exc.code

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        if result.get("ok"):
            print(f"{result['action']}: ok")
            if result.get("install_dir"):
                print(f"  install: {result['install_dir']}")
            if result.get("runtime_config"):
                print(f"  runtime: {result['runtime_config']}")
            for problem in result.get("problems", []):
                print(f"  problem: {problem}")
        else:
            error = result.get("error", {})
            print(f"{result['action']}: {error.get('message')}", file=sys.stderr)
            if error.get("detail"):
                print(f"  {error['detail']}", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main())
