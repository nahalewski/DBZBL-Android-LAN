#!/usr/bin/env python3
"""Package the Apple Silicon beta app without game content or runtime data."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    'Contents/Info.plist',
    'Contents/Resources/BurstLimit.icns',
    'Contents/MacOS/burstlimit',
    'Contents/MacOS/librexruntime.dylib',
    'Contents/MacOS/librexgpu-xenos.dylib',
    'Contents/MacOS/vulkan/lib/libMoltenVK.dylib',
    'Contents/MacOS/vulkan/lib/libvulkan.1.dylib',
    'Contents/MacOS/vulkan/share/vulkan/icd.d/MoltenVK_icd.json',
)
# Vulkan discovery already supports Resources/vulkan. Runtime data such as
# icd.d belongs in Resources; a dotted folder under MacOS is treated as code
# by codesign and prevents a valid bundle seal.
DESTINATIONS = {name: name.replace('Contents/MacOS/vulkan/', 'Contents/Resources/vulkan/')
                for name in FILES}
BINARIES = tuple(DESTINATIONS[name] for name in FILES if name.endswith('.dylib'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path,
                        default=ROOT / 'out/build/mac-arm64-release/burstlimit.app')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/releases/v1.0-beta')
    args = parser.parse_args()
    app, output = args.app.resolve(), args.output.resolve()
    for name in FILES:
        if not (app / name).is_file():
            raise SystemExit(f'Missing required app file: {name}; build with macos/build.sh first.')
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    if info.get('CFBundleShortVersionString') != '1.0' or info.get('CFBundleVersion') != '1':
        raise SystemExit('Build the 1.0 beta app before packaging.')
    output.mkdir(parents=True, exist_ok=True)
    archive = output / 'DBZ-Burst-Limit-1.0-beta-macos-arm64.zip'
    with tempfile.TemporaryDirectory(prefix='mac-beta-package-', dir=output) as temp:
        staged = Path(temp) / 'Burst Limit.app'
        for name in FILES:
            destination = staged / DESTINATIONS[name]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(app / name, destination)
        for name in BINARIES:
            subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none',
                            str(staged / name)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none',
                        '--identifier', 'org.burstlimit.macos', str(staged)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', '--verbose=2',
                        str(staged)], check=True)
        subprocess.run(['ditto', '-c', '-k', '--keepParent', '--norsrc',
                        str(staged), str(archive)], check=True)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    report = {'archive': archive.name, 'sha256': digest, 'bytes': archive.stat().st_size,
              'version': '1.0 beta', 'signing': 'ad-hoc; not notarized',
              'included_files_before_signing': list(DESTINATIONS.values()), 'game_data_included': False}
    (output / 'mac-package-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Created {archive.name} ({archive.stat().st_size:,} bytes)')
    print(f'SHA256: {digest}')


if __name__ == '__main__':
    main()
