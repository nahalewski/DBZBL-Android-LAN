#!/usr/bin/env python3
"""Exercise production LAN discovery sockets, metadata, filtering and live rename."""
import argparse
import json
import os
import select
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    args = parser.parse_args()
    # macOS routes only the configured 127.0.0.1 loopback alias by default.
    # Give the searcher a distinct advertised identity to exercise self filtering.
    env = dict(os.environ, REX_XNET_IP="127.0.0.2", REX_XNET_SEARCH_IP="127.0.0.1")
    env.pop("REX_LAN_DISABLED", None)
    host = subprocess.Popen([args.probe, "host"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            text=True, env=env)

    def line():
        assert select.select([host.stdout], [], [], 5)[0], "Host did not answer"
        return host.stdout.readline().strip()

    def command(value):
        host.stdin.write(value + "\n")
        host.stdin.flush()
        assert line() == "OK"

    def query(packet):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.settimeout(.3)
            udp.sendto(packet, ("127.0.0.1", 36001))
            try:
                return json.loads(udp.recv(4096))
            except socket.timeout:
                return None

    request = {"protocol": "burstlimit-lan/1", "type": "find", "nonce": 123,
               "version": "lan-integration-test"}
    try:
        assert line() == "READY"
        # Start is asynchronous, so query with a bounded retry.
        room = None
        for _ in range(10):
            room = query(json.dumps(request).encode())
            if room:
                break
        assert room and room["name"] == "MacHost" and room["nonce"] == 123
        assert room["kid"] == [174, 127, 0, 0, 2, 82, 88, 1]
        assert room["players"] == 1 and room["max"] == 2
        assert dict(room["contexts"])[3] == 1 and dict(room["properties"])[0x1000001E] == 7
        found = subprocess.check_output([args.probe, "search"], env=env, text=True)
        assert "ROOM MacHost " in found and "COUNT 1" in found
        subprocess.run([args.probe, "empty"], env=env, check=True)
        for invalid in [b"not json", b"[]", b"{}", json.dumps(dict(request, version="wrong")).encode(),
                        json.dumps(dict(request, nonce="bad")).encode(), b"x" * 5000]:
            assert query(invalid) is None, "Invalid / incompatible query answered"
        command("rename")
        assert query(json.dumps(request).encode())["name"] == "NewTag"
        command("full")
        assert query(json.dumps(request).encode()) is None
        command("open")
        assert query(json.dumps(request).encode())["players"] == 1
        command("delete")
        assert query(json.dumps(request).encode()) is None
        command("private")
        assert query(json.dumps(request).encode()) is None
        disabled = subprocess.check_output([args.probe, "search"],
                                          env=dict(env, REX_LAN_DISABLED="1"), text=True)
        assert "COUNT 0" in disabled
        print("PASS: production UDP discovery, host name/rules, compatible search, malformed packets, "
              "live gamertag, unchanged storage identity, full/private/deleted rooms, offline mode")
    finally:
        host.stdin.write("quit\n")
        host.stdin.flush()
        host.wait(timeout=5)
        assert host.returncode == 0


if __name__ == "__main__":
    main()
