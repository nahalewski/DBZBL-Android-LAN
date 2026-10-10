# Apple Silicon Mac build and usage

The Mac port is a native **`burstlimit.app`** application. The shell scripts are optional source-build launchers. The complete app includes its runtime libraries, Xenos GPU plugin, Vulkan loader and MoltenVK. Original game files remain outside the bundle.

## Requirements

To run:

- Apple Silicon **M1 or newer**, running an ARM64 Mac system. Intel Macs and Rosetta are not validated targets.
- The current locally built app and bundled libraries declare **macOS 26.5** as their minimum deployment version. Testing was performed on **macOS 27.2 with an M1 Pro**. Lower OS deployment targets have not been validated; the current preset follows the selected Apple SDK/toolchain.
- About **3.4 GB extra space** for extracted game data, plus the app, saves and your original ISO/ZIP. 8 GB RAM or more is recommended for testing, without a proven universal minimum.
- Your own **USA/NTSC-U Xbox 360** game ISO or a ZIP of extracted game files. Supported `default.xex`: 7,983,104 bytes, SHA-1 `aec598f88cf51181fc377b148e0b1ad30db4485c`.
- Keyboard or SDL-compatible gamepad. The physical Mac gamepad path is implemented but has not been exercised with a real controller in the current validation.
- For LAN: another compatible Android/Mac build on the same local IPv4 network, with firewall access.

A packaged app does not need Python, Homebrew or an external extractor to play. MoltenVK and the Vulkan loader are bundled, and ZIP import links macOS's system libarchive.

## Open the app and install your game

1. Obtain a complete app built from this source. The first release provides an Android APK; the Mac app currently requires a source build. For LAN with the 1.0 beta APK, check out the matching `v1.0-beta` tag and build it on Mac.
2. Copy **the complete `burstlimit.app`** to a writable location such as Applications. Do not copy only the executable; the runtime and graphics libraries must stay together.
3. Open it in Finder. Local development builds are not notarized; if macOS blocks an app you built and trust, use the system's Open/Privacy & Security approval flow.
4. On first launch without valid game files, a native file picker asks for your USA `.iso` or a `.zip` containing extracted game files. A ZIP containing an ISO needs unpacking first. 7z/RAR wrappers are not imported directly.
5. Setup verifies the USA executable, shows progress and allows cancellation. It keeps the original archive and publishes game files after the copy verifies successfully. Subsequent launches reuse the installation.

Finder-launched data lives in:

```text
~/Library/Application Support/Burst Limit/
├── game_data_root/
│   ├── default.xex
│   └── LONG2DATA/
└── ... settings, logs and user data ...
```

The file picker and importer run before the game renderer starts. A valid existing installation skips the picker. If an invalid/nonempty destination exists, setup refuses to overwrite it: preserve or move that directory, then retry. Removing the `.app` alone does not remove Application Support data.

## Controls and gamertag

Finder launches default to the tested window/renderer and keyboard settings unless saved configuration or explicit options override them. The source launcher accepts `--mnk_mode=true`.

| Key | Xbox input |
| --- | --- |
| Enter | Start |
| Space | A / confirm |
| Backspace | B / back |
| WASD | Left stick / movement |
| L | X |
| P | Y |
| Q or I | LT |
| E or O | RT |
| 1 | LB |
| 3 | RB |

F1, or Back + Start on a controller, opens runtime settings. SDL gamepad support is included. Input goes to the focused game window.

For the source launcher, set a persistent gamertag:

```sh
mkdir -p out/mac-user-data
printf '%s\n' 'Mac M1' > out/mac-user-data/gamertag.txt
./macos/run.sh --mnk_mode=true
```

`macos/run.sh` reads that file on launch. Alternatively, pass `--online_gamertag='YourName'`. Finder launches use the separate Application Support profile; set `online_gamertag = "YourName"` in that profile's `burstlimit.toml` with the app closed. Use a printable name of 1–15 characters to match Android's constraints. Android has an in-game live editor; no equivalent native Mac gamertag dialog is currently provided.

## LAN fighting with Android or another Mac

LAN discovery and direct play are available without Xbox services. Keep compatible builds on the same network. Create a public Player Match on the host; search Custom Match on the guest; both players choose Ready. [Full instructions](../docs/LAN.md).

For a source build, double-click **`macos/host-lan.command`**, or run:

```sh
./macos/host-lan.command
```

It detects the active network IPv4, selects direct LAN mode and the tested renderer, and leaves the game socket bound to all interfaces. It opens the game; you still create the public room in its menu. The same discovery configuration can also search another host's room through Custom Match.

Do not set `REX_XNET_BIND_IP` for the normal one-instance-per-device setup. Burst Limit sends a startup control packet to itself at `127.0.0.1`; binding only the Wi-Fi address prevents it arriving. `REX_XNET_IP` advertises the local address without changing the wildcard bind. An optional `REX_XNET_SEARCH_IP` specifies a host manually. The startup script clears stale bind/search/offline overrides before starting discovery.

## Build from source

Build prerequisites:

| Tool | Requirement |
| --- | --- |
| Apple Silicon Mac | Native ARM64 build |
| Xcode toolchain | Apple Clang 16+/Xcode 16+ with a compatible macOS SDK; full Xcode/Metal toolchain may be needed by MoltenVK |
| CMake | 3.25 or newer; current development uses 4.3.1 |
| Ninja | Installed and on PATH |
| Python | 3 |
| Git | Recursive submodules and patch application |
| libarchive headers | Homebrew `libarchive`; only headers are required from Homebrew for the app importer |
| Game executable | Verified USA `default.xex`, extracted locally |
| Free disk | Plan for tens of GB for dependencies, generated code and builds |

With the Apple developer tools installed, install the non-Apple build tools:

```sh
brew install cmake ninja python libarchive
git clone --recursive https://github.com/nahalewski/DBZBL-Android-LAN.git
cd DBZBL-Android-LAN
git checkout v1.0-beta
git submodule update --init --recursive
python3 scripts/apply_android_sdk_patch.py
```

The shared SDK patch is pinned to `66de2e369bc98c85e9e79c7128b19f49a1f6a967`. Apply it before configuring any host or device build. The helper checks for conflicts and does not reset your edits.

Prepare `game_data_root/default.xex` and the complete `LONG2DATA` directory at the repository root. For an ISO:

```sh
python3 scripts/extract_android_game.py '/absolute/path/to/USA.iso'
```

For a ZIP of extracted game files, unpack it locally into that layout. Verify the executable with `shasum -a 1 game_data_root/default.xex`. These local files and generated output stay ignored by Git.

Build:

```sh
./macos/build.sh
```

This applies the pinned MoltenVK drawable-lifetime patch, configures the `mac-arm64-release` preset, runs the host code generator and compiles the native app. The preset selects `/usr/bin/clang` and `/usr/bin/clang++`; avoid older external LLVM versions that cannot read the selected Apple SDK. Set `BURSTLIMIT_BUILD_JOBS` to reduce parallel build memory use if needed.

The script configures again after code generation so a fresh checkout includes the emitted game sources in its build graph.

Output:

```text
out/build/mac-arm64-release/burstlimit.app
```

Open it in Finder for normal first-run installation, or use the source launcher:

```sh
./macos/run.sh --mnk_mode=true --vulkan_dynamic_rendering=false
# Optional external extracted-game folder:
./macos/run.sh '/absolute/path/to/game_data_root' --mnk_mode=true
```

The source launcher uses a 1280×720 window, 720p internal rendering, 30 FPS and synchronous shader compilation. Its settings/saves use **`out/mac-user-data`**, separate from the Finder profile. Its default game path is the local repository's `game_data_root`; a valid folder skips setup. Missing data invokes setup for the selected destination.

For unattended import, the native executable also accepts `--install_iso=/absolute/path/to/USA.iso`, or an extracted-game ZIP at that option's path. The executable remains inside `Contents/MacOS`; moving the whole app is required.

## Validation and limitations

Bounded M1 Pro tests covered startup, first-launch USA ZIP installation, executable/file verification, keyboard navigation, offline CPU gameplay and two completed 60-second LAN rounds with first-generation Pixel Fold. Attacks and movement were visible in both directions. Native importer tests cover ISO partition offsets, ZIP layouts, invalid/truncated inputs, traversal/cycles, cancellation and preserving existing files.

```sh
mkdir -p out
clang++ -std=c++23 -fsanitize=address,undefined -Isrc \
  -I/opt/homebrew/opt/libarchive/include tests/game_import_probe.cpp \
  src/burstlimit_game_import.cpp -larchive -o out/game-import-probe
python3 scripts/test_mac_game_import.py out/game-import-probe game_data_root/default.xex
```

Full campaign, long sessions, audio and broad gamepad/OS coverage remain unverified. MoltenVK/rendering failures were observed in earlier stress checks; the current port should not be described as crash-free. Keep `vulkan_dynamic_rendering=false`, normal shaders and diagnostic readback disabled for regular play. Fragment-shader-interlock renderer overrides remain experimental. See [compatibility](../docs/compatibility.md) and [LAN troubleshooting](../docs/LAN.md).
