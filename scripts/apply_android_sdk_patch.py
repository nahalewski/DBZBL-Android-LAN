#!/usr/bin/env python3
"""Reapply the Android and Mac SDK changes after cloning the pinned submodule."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "thirdparty" / "rexglue-sdk"
PATCH = ROOT / "patches" / "rexglue-android.patch"
PINNED = "66de2e369bc98c85e9e79c7128b19f49a1f6a967"


def git(*args, check=False):
    return subprocess.run(["git", "-C", str(SDK), *args], check=check, capture_output=True, text=True)


def main():
    if git("rev-parse", "HEAD", check=True).stdout.strip() != PINNED:
        raise SystemExit("SDK revision differs from the tested revision; review/rebase the patch first.")
    if git("apply", "--reverse", "--check", str(PATCH)).returncode == 0:
        print("Android/Mac SDK patch is already applied.")
        return
    result = git("apply", "--check", str(PATCH))
    if result.returncode:
        raise SystemExit("Patch conflicts with local SDK changes; no files changed.\n" + result.stderr)
    git("apply", str(PATCH), check=True)
    print("Android/Mac SDK patch applied.")


if __name__ == "__main__":
    main()
