#!/usr/bin/env python3
"""Explicit-device Android install, data transfer and evidence collection."""
import argparse
import json
import ipaddress
import re
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="Wireless IP:port or adb serial; never auto-selects a device")
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--game-data", type=Path)
    parser.add_argument("--verify-manifest", type=Path,
                        help="Verify every device game file against a local relative-path SHA-256 JSON manifest")
    parser.add_argument("--package", default="org.burstlimit.android.probe")
    parser.add_argument("--capture", type=Path, default=ROOT / "out" / "android-device")
    parser.add_argument("--launch", action="store_true")
    parser.add_argument("--lan-role", choices=("auto", "offline", "host", "join"), help="Persist a game LAN role at launch")
    parser.add_argument("--lan-host", help="Host IPv4 when the LAN role is join")
    parser.add_argument("--probe-udp-peer", help="IPv4 of an echo server on UDP 61198 (diagnostic APK only)")
    args = parser.parse_args()
    for address in (args.lan_host, args.probe_udp_peer):
        if address:
            try:
                parsed = ipaddress.IPv4Address(address)
                if parsed.is_unspecified or parsed.is_multicast or str(parsed) == "255.255.255.255":
                    raise ValueError("Requires unicast IPv4")
            except ValueError:
                parser.error("Invalid unicast IPv4: " + address)
    if args.lan_role and (not args.launch or args.package.endswith(".probe")):
        parser.error("LAN roles require --launch with the game package")
    if args.lan_role == "join" and not args.lan_host:
        parser.error("The join role requires --lan-host")
    if args.probe_udp_peer and (not args.launch or not args.package.endswith(".probe")):
        parser.error("UDP diagnostics require --launch with the probe package")

    def adb(*values, check=True):
        return subprocess.run(["adb", "-s", args.serial, *values], text=True, capture_output=True, check=check)

    if adb("get-state").stdout.strip() != "device":
        parser.error("Selected device is not connected/authorized")
    info = {}
    for name in ("ro.product.model", "ro.product.device", "ro.build.version.release", "ro.build.version.sdk", "ro.product.cpu.abilist", "ro.board.platform"):
        info[name] = adb("shell", "getprop", name).stdout.strip()
    info["page_size"] = adb("shell", "getconf", "PAGE_SIZE").stdout.strip()
    print(json.dumps(info, indent=2), flush=True)
    if "arm64-v8a" not in info["ro.product.cpu.abilist"]:
        parser.error("This APK requires arm64-v8a")
    if args.apk:
        print(adb("install", "-r", str(args.apk.resolve())).stdout)
    if args.game_data:
        if args.package.endswith(".probe"):
            parser.error("Use --package org.burstlimit.android when transferring real game data")
        import hashlib
        xex = args.game_data / "default.xex"
        if not xex.is_file() or hashlib.sha1(xex.read_bytes()).hexdigest() != "aec598f88cf51181fc377b148e0b1ad30db4485c":
            parser.error("Game data has no verified USA default.xex")
        if any(not (args.game_data / "LONG2DATA" / name).is_file()
               for name in ("LONG2DATA_US.CPK", "STREAM_US.CPK", "STREAM_JP.CPK")):
            parser.error("Game data is missing a required LONG2DATA archive")
        destination = f"/sdcard/Android/data/{args.package}/files/game_data_root"
        adb("shell", "mkdir", "-p", destination)
        print("Transferring verified USA game files (existing current files are skipped).", flush=True)
        print(adb("push", "--sync", str(args.game_data.resolve()) + "/.", destination + "/").stdout)
        # adb creates directories as shell:ext_data_rw with mode 2770. The app
        # UID needs traversal permission even though the files are readable.
        adb("shell", "find", destination, "-type", "d", "-exec", "chmod", "755", "{}", "+")
    verification = None
    if args.verify_manifest:
        if args.package.endswith(".probe"):
            parser.error("Use the game package when verifying game data")
        manifest = json.loads(args.verify_manifest.read_text())
        if not manifest:
            parser.error("Game-data verification manifest is empty")
        destination = f"/sdcard/Android/data/{args.package}/files/game_data_root"
        paths = {}
        for relative, entry in manifest.items():
            relative_path = Path(relative)
            if relative_path.is_absolute() or ".." in relative_path.parts:
                parser.error("Manifest paths must remain inside game_data_root")
            expected = entry.get("sha256", "")
            if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
                parser.error("Manifest contains an invalid SHA-256 digest")
            paths[destination + "/" + relative] = expected
        print(f"Verifying SHA-256 of {len(paths)} game files on {args.serial}.", flush=True)
        values = tuple(shlex.quote(path) for path in paths)
        checksums = adb("shell", "sha256sum", *values, check=False)
        # The first-run importer publishes an app-owned private directory.
        # Debug builds permit read-only verification as the actual app UID.
        verification_method = "shell"
        if checksums.returncode != 0:
            checksums = adb("shell", "run-as", args.package, "sha256sum", *values)
            verification_method = "run-as"
        actual = {}
        for line in checksums.stdout.splitlines():
            digest, path = line.split(None, 1)
            actual[path.lstrip(" *")] = digest
        if actual != paths:
            mismatches = [path for path, expected in paths.items() if actual.get(path) != expected]
            parser.error("Game-data SHA-256 mismatch: " + ", ".join(mismatches))
        verification = {"serial": args.serial, "files": len(paths), "result": "PASS", "method": verification_method, "manifest": manifest}
        print(f"Game-data SHA-256: PASS ({len(paths)} files).", flush=True)
    if args.launch:
        if args.package.endswith(".probe"):
            adb("shell", "rm", "-f", f"/sdcard/Android/data/{args.package}/files/runtime-probe.txt")
        extras = []
        if args.lan_role:
            extras += ["--es", "lan_role", args.lan_role]
            if args.lan_host:
                extras += ["--es", "lan_host", args.lan_host]
        if args.probe_udp_peer:
            extras += ["--es", "probe_udp_peer", args.probe_udp_peer]
        result = adb("shell", "am", "start", "-S", "-n", args.package + "/org.burstlimit.android.GameSetupActivity", *extras)
        print(result.stdout)
        if "Error" in result.stderr or "Error" in result.stdout:
            parser.error("Activity failed to launch")
        if args.package.endswith(".probe"):
            deadline = time.monotonic() + 55
            while time.monotonic() < deadline:
                fresh = adb("shell", "cat", f"/sdcard/Android/data/{args.package}/files/runtime-probe.txt", check=False)
                if fresh.returncode == 0:
                    break
                time.sleep(1)
            else:
                print("Probe did not finish within 55 seconds; collecting failure evidence.", flush=True)
        else:
            time.sleep(8)
    args.capture.mkdir(parents=True, exist_ok=True)
    (args.capture / "runtime-probe.txt").unlink(missing_ok=True)
    if verification:
        (args.capture / "data-verification.json").write_text(json.dumps(verification, indent=2) + "\n")
    uid = adb("shell", "cmd", "package", "list", "packages", "-U", args.package).stdout
    # adb's package filter also returns org.burstlimit.android.probe when the
    # game package is requested. Match the exact name before collecting logs.
    uid_value = next((line.split(" uid:", 1)[1].strip()
                      for line in uid.splitlines()
                      if line.startswith(f"package:{args.package} uid:")), None)
    if not uid_value or not uid_value.isdigit():
        parser.error("Package UID unavailable; install the requested package before collecting logs")
    logs = adb("logcat", "-d", "-v", "threadtime", "--uid=" + uid_value).stdout
    (args.capture / "logcat.txt").write_text(logs)
    report = adb("shell", "cat", f"/sdcard/Android/data/{args.package}/files/runtime-probe.txt", check=False)
    probe_failed = args.launch and args.package.endswith(".probe") and (
        report.returncode != 0 or ": FAIL" in report.stdout)
    if report.returncode == 0:
        (args.capture / "runtime-probe.txt").write_text(report.stdout)
        print(report.stdout)
    screenshot = subprocess.run(["adb", "-s", args.serial, "exec-out", "screencap", "-p"], capture_output=True, check=True)
    png_offset = screenshot.stdout.find(b"\x89PNG\r\n\x1a\n")
    if png_offset < 0:
        raise RuntimeError("screencap returned no PNG")
    (args.capture / "screenshot.png").write_bytes(screenshot.stdout[png_offset:])
    state = adb("shell", "pidof", args.package, check=False)
    activity = adb("shell", "dumpsys", "activity", "activities", check=False).stdout
    foreground = next((line.strip() for line in activity.splitlines() if "topResumedActivity=" in line), "unavailable")
    info["foreground_activity"] = foreground
    match = re.search(r"\s([A-Za-z0-9._]+)/", foreground)
    info["app_foreground"] = bool(match and match.group(1) == args.package)
    (args.capture / "device.json").write_text(json.dumps(info, indent=2) + "\n")
    print("App process: " + (state.stdout.strip() or "not running"))
    if not info["app_foreground"]:
        print("App is not foreground; the screenshot does not verify its UI. Foreground: " + foreground)
    print("Evidence: " + str(args.capture.resolve()))
    if probe_failed:
        raise SystemExit("Runtime probe failed or did not finish; inspect the captured evidence.")
    if args.launch and not args.package.endswith(".probe") and not state.stdout.strip():
        raise SystemExit("Game process exited after launch; inspect the captured evidence.")


if __name__ == "__main__":
    main()
