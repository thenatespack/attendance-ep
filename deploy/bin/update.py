#!/usr/bin/env python3
"""Checks GitHub for a new attendance-ep release, downloads the prebuilt
binary matching this machine's architecture, swaps it into place, and
restarts attendance-kiosk.service. If the new binary doesn't survive a
short grace window after restart, it's rolled back automatically.

Run periodically by attendance-update.timer (see deploy/systemd/). Meant to
run as root: it writes into INSTALL_DIR and controls the kiosk unit.
"""

import fcntl
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

INSTALL_DIR = "/opt/attendance-ep"
BIN_PATH = os.path.join(INSTALL_DIR, "imgui-test")
PREV_PATH = os.path.join(INSTALL_DIR, "imgui-test.prev")
STATE_DIR = os.path.join(INSTALL_DIR, "state")
INSTALLED_VERSION_FILE = os.path.join(STATE_DIR, "installed_version")
FAILED_VERSION_FILE = os.path.join(STATE_DIR, "failed_version")
LOCK_PATH = "/run/attendance-update.lock"

REPO = "thenatespack/attendance-ep"
SERVICE_NAME = "attendance-kiosk.service"
GRACE_SECONDS = 10
POLL_INTERVAL_SECONDS = 1
HTTP_TIMEOUT_SECONDS = 15

ARCH_MAP = {
    "aarch64": "arm64",
    "x86_64": "amd64",
}


def log(message):
    print(f"[update] {message}", flush=True)


def log_error(message):
    print(f"[update] ERROR: {message}", file=sys.stderr, flush=True)


def read_text_file(path):
    try:
        with open(path, "r") as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def atomic_write_text(path, text):
    fd, tmp_path = tempfile.mkstemp(dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "w") as f:
            f.write(text)
        os.replace(tmp_path, path)
    except BaseException:
        os.unlink(tmp_path)
        raise


def detect_arch():
    machine = platform.machine()
    arch = ARCH_MAP.get(machine)
    if arch is None:
        log_error(f"unsupported architecture '{machine}' (expected aarch64 or x86_64)")
        sys.exit(1)
    return arch


def fetch_latest_release():
    url = f"https://api.github.com/repos/{REPO}/releases/latest"
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": "attendance-ep-updater",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
            return json.load(response)
    except (urllib.error.URLError, urllib.error.HTTPError, json.JSONDecodeError) as exc:
        log_error(f"could not fetch latest release: {exc}")
        sys.exit(1)


def find_asset(release, arch):
    asset_name = f"imgui-test-linux-{arch}"
    for asset in release.get("assets", []):
        if asset.get("name") == asset_name:
            return asset
    log_error(f"release {release.get('tag_name')} has no asset named '{asset_name}'")
    sys.exit(1)


def download_asset(asset, dest_dir):
    fd, tmp_path = tempfile.mkstemp(dir=dest_dir)
    os.close(fd)
    request = urllib.request.Request(
        asset["browser_download_url"],
        headers={"User-Agent": "attendance-ep-updater"},
    )
    try:
        with (
            urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response,
            open(tmp_path, "wb") as out,
        ):
            shutil.copyfileobj(response, out)
    except (urllib.error.URLError, urllib.error.HTTPError) as exc:
        os.unlink(tmp_path)
        log_error(f"download failed: {exc}")
        sys.exit(1)

    expected_size = asset.get("size")
    actual_size = os.path.getsize(tmp_path)
    if actual_size == 0 or (expected_size is not None and actual_size != expected_size):
        os.unlink(tmp_path)
        log_error(
            f"downloaded size {actual_size} doesn't match expected {expected_size}"
        )
        sys.exit(1)

    os.chmod(tmp_path, 0o755)
    return tmp_path


def systemctl(*args):
    return subprocess.run(
        ["systemctl", *args], capture_output=True, text=True, check=False
    )


def get_unit_property(name):
    result = systemctl("show", SERVICE_NAME, f"--property={name}")
    for line in result.stdout.splitlines():
        key, _, value = line.partition("=")
        if key == name:
            return value
    return None


def restart_kiosk():
    result = systemctl("restart", SERVICE_NAME)
    if result.returncode != 0:
        log_error(f"systemctl restart {SERVICE_NAME} failed: {result.stderr.strip()}")


def health_check():
    baseline_restarts = get_unit_property("NRestarts")
    deadline = time.monotonic() + GRACE_SECONDS
    while time.monotonic() < deadline:
        active_state = get_unit_property("ActiveState")
        restarts = get_unit_property("NRestarts")

        if active_state == "failed":
            return False, "unit reached failed state"
        if restarts != baseline_restarts:
            return False, (
                f"systemd auto-restarted the unit during the grace window "
                f"(NRestarts {baseline_restarts} -> {restarts}), meaning it crashed"
            )

        time.sleep(POLL_INTERVAL_SECONDS)

    active_state = get_unit_property("ActiveState")
    if active_state != "active":
        return False, f"unit not active after grace window (ActiveState={active_state})"
    return True, None


def main():
    os.makedirs(STATE_DIR, exist_ok=True)

    with open(LOCK_PATH, "w") as lock_file:
        try:
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            log("another update run is already in progress, skipping")
            return 0

        arch = detect_arch()
        release = fetch_latest_release()
        latest_tag = release.get("tag_name")
        if not latest_tag:
            log_error("release response has no tag_name")
            return 1

        installed_version = read_text_file(INSTALLED_VERSION_FILE)
        if installed_version == latest_tag:
            log(f"already up to date ({installed_version})")
            return 0

        failed_version = read_text_file(FAILED_VERSION_FILE)
        if failed_version == latest_tag:
            log(
                f"release {latest_tag} previously failed its health check -- "
                "skipping until a newer release is published"
            )
            return 0

        asset = find_asset(release, arch)
        log(f"downloading {asset['name']} for release {latest_tag}")
        tmp_path = download_asset(asset, INSTALL_DIR)

        if os.path.exists(BIN_PATH):
            shutil.copy2(BIN_PATH, PREV_PATH)
        os.replace(tmp_path, BIN_PATH)

        log(f"installed {latest_tag}, restarting {SERVICE_NAME}")
        restart_kiosk()

        healthy, reason = health_check()
        if healthy:
            atomic_write_text(INSTALLED_VERSION_FILE, latest_tag)
            if os.path.exists(FAILED_VERSION_FILE):
                os.remove(FAILED_VERSION_FILE)
            log(f"update succeeded: {installed_version} -> {latest_tag}")
            return 0

        log_error(f"health check failed for {latest_tag}: {reason}")
        log_error("rolling back to previous binary")
        if os.path.exists(PREV_PATH):
            shutil.copy2(PREV_PATH, BIN_PATH)
            restart_kiosk()
        else:
            log_error("no previous binary to roll back to!")
        atomic_write_text(FAILED_VERSION_FILE, latest_tag)
        log(f"rolled back; installed version remains {installed_version}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
