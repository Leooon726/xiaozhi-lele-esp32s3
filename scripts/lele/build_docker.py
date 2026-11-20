#!/usr/bin/env python3
"""Build the current Lele baseline in Docker; see docs/lele/SETUP.md."""

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
IMAGE = "espressif/idf:v6.1.1"


def main() -> int:
    docker = shutil.which("docker")
    if docker is None:
        print("Docker is missing. See docs/lele/SETUP.md.", file=sys.stderr)
        return 1
    info = subprocess.run(
        [docker, "info", "--format", "{{.OSType}}"],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if info.returncode:
        print(info.stderr, file=sys.stderr)
        print("Start Docker and check access with 'docker info'. See docs/lele/SETUP.md.", file=sys.stderr)
        return 1
    if info.stdout.strip() != "linux":
        print("Docker must use Linux containers. See docs/lele/SETUP.md.", file=sys.stderr)
        return 1
    if "," in str(ROOT):
        print("Use a checkout path without commas (Docker bind mount syntax).", file=sys.stderr)
        return 1

    for folder in ("docker-home", "component-cache", "logs"):
        (ROOT / ".local" / folder).mkdir(parents=True, exist_ok=True)
    lock = ROOT / "dependencies.lock"
    if not lock.exists():
        shutil.copyfile(ROOT / "config/lele/dependencies.lock", lock)

    command = [docker, "run", "--rm"]
    if os.name == "posix":
        command += ["--user", f"{os.getuid()}:{os.getgid()}"]
    command += [
        "-e", "HOME=/project/.local/docker-home",
        "-e", "IDF_COMPONENT_CACHE_PATH=/project/.local/component-cache",
        "-e", "IDF_GIT_SAFE_DIR=/project",
        "-e", "XIAOZHI_BUILD_STAGES=1",
        "--mount", f"type=bind,source={ROOT},target=/project",
        "-w", "/project", IMAGE,
        "python", "-u", "scripts/build.py", "lckfb/szpi-esp32s3",
        "--name", "lichuang-dev-lele-music-debug", "--language", "en-US",
        "--wake-word", "custom:le le:乐乐", "--zip",
    ]
    print(f"Building with {IMAGE}; log: .local/logs/build-docker.log", flush=True)
    with (ROOT / ".local/logs/build-docker.log").open("wb") as log:
        with subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
            for line in process.stdout:
                log.write(line)
                log.flush()
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()
            return process.wait()


if __name__ == "__main__":
    sys.exit(main())
