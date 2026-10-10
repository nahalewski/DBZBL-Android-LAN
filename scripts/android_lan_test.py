#!/usr/bin/env python3
"""Verify the Android Xbox socket backend over LAN; this does not run a match."""
import argparse
import ipaddress
import json
from pathlib import Path
import socket
import subprocess
import sys
import threading

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--local-ip", required=True, help="This computer's IPv4 on the device's LAN")
    parser.add_argument("--capture", type=Path, required=True)
    args = parser.parse_args()
    ipaddress.IPv4Address(args.local_ip)
    args.capture.mkdir(parents=True, exist_ok=True)
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind((args.local_ip, 61198))
    server.settimeout(.25)
    stop = threading.Event()
    packets = []
    def echo():
        while not stop.is_set():
            try:
                data, peer = server.recvfrom(1500)
                if len(data) == 320 and data.startswith(b"BurstLimitLANProbe"):
                    server.sendto(data, peer)
                    packets.append({"peer": list(peer), "sequence": data[18], "bytes": len(data)})
            except socket.timeout:
                continue
    worker = threading.Thread(target=echo, daemon=True)
    worker.start()
    result = None
    try:
        result = subprocess.run([sys.executable, str(Path(__file__).with_name("android_device.py")),
                "--serial", args.serial, "--launch", "--probe-udp-peer", args.local_ip,
                "--capture", str(args.capture)])
    finally:
        stop.set()
        worker.join(1)
        server.close()
        report = (args.capture / "runtime-probe.txt")
        passed = result is not None and result.returncode == 0 and report.exists() and \
                 "LAN UDP game-port round trips: PASS" in report.read_text() and \
                 len({p["sequence"] for p in packets}) == 32
        summary = {"result": "PASS" if passed else "FAIL", "scope": "Android Xbox socket LAN diagnostic; no game match tested",
                   "echo_server": [args.local_ip, 61198], "packets": packets}
        (args.capture / "lan-echo.json").write_text(json.dumps(summary, indent=2) + "\n")
    if not passed:
        raise SystemExit("LAN socket diagnostic failed; inspect captured evidence")

if __name__ == "__main__":
    main()
