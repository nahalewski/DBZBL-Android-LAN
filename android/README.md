# Android build and usage

## Requirements

To play, use a 64-bit ARM Android device with **Android 11/API 30 or newer** and **Vulkan 1.1 or newer**. The renderer also requires device features such as `fragmentStoresAndAtomics`; Vulkan availability alone does not ensure the full renderer works. The APK contains only `arm64-v8a`, with no 32-bit ARM or x86 build.

Allow about **3.4 GB extra free space** for extracted USA game files, plus the APK, saves and source archive. A recent device with 8 GB RAM or more is recommended for testing; no universal RAM minimum or sustained frame-rate target has been established.

Supported game: your own USA/NTSC-U Xbox 360 release, with `default.xex` size 7,983,104 bytes and SHA-1 `aec598f88cf51181fc377b148e0b1ad30db4485c`. PAL and Japanese versions cannot be substituted.

Mali GPUs have compressed BC2/BC3 texture decode fallbacks and compute-based vertex exports when vertex-stage stores are unavailable. Certain indexed/tessellated memory export cases remain unsupported. [Compatibility results](../docs/compatibility.md) describe the exact testing scope for Pixel Fold, Pixel 9 Pro Fold and Galaxy Tab S11 Ultra. Razer Edge and other GPU/device families are unverified.

## Install and first launch

1. Obtain a **game APK built from this source**. This repository initially publishes source; no APK is stored in Git.
2. Copy your USA game ISO, or ZIP containing extracted game files, to the device's Downloads folder. Keep the complete game content. A ZIP containing an ISO must be unpacked before selecting the ISO; 7z/RAR archives must be unpacked first too.
3. Open the APK in Android's file manager and allow that file manager to **install unknown apps** when Android asks. Alternatively, use `adb install -r` as shown below.
4. Open **Burst Limit**. The first-launch file picker appears when a valid game installation is missing. Select your `.iso` or extracted-game `.zip`.
5. Wait for progress to finish. Setup verifies the USA executable and required archives before publishing the installation. Canceling or a failed import cleans up the temporary copy. The original archive remains unchanged.
6. Later launches reuse the valid installation. An app update preserves it when installed with the same package and signing key.

Use a local file provider for ISO files: the importer needs random/seekable access. ZIPs are read twice through the granted URI without caching a second full source archive. Setup uses the system picker and does not ask for broad storage permission. If cloud access fails, download the file completely to local Downloads and retry.

The installed game normally resides in:

```text
/sdcard/Android/data/org.burstlimit.android/files/game_data_root
```

Android restricts direct access to this directory from file managers. Let the built-in installer create it. Saves/settings use app storage, including `files/user_data`; clearing app data or uninstalling can remove them. The diagnostic APK uses a separate `.probe` package and does not run the game.

## Touch controls and gamepads

Tap **Controls** at the top to toggle on-screen controls, configure LAN or change the gamertag. Touch controls include two analog sticks, D-pad diagonals, A/B/X/Y, LB/RB, LT/RT, L3/R3, Back and Start. Their layout adapts to screen size and avoids system bars/cutouts. Hidden or canceled gestures release their input.

Connect an Android-recognized USB or Bluetooth gamepad. Android detects attachment/removal and SDL handles buttons, sticks and triggers. Touch controls hide automatically while a gamepad is present and return after the last controller disconnects when the saved visibility setting is on. USB takeover/input was tested with a Razer Kishi V2 Pro on Pixel Fold. Physical Bluetooth-controller testing remains outstanding.

**Back + Start** opens runtime settings. **Controls → Gamertag** accepts 1–15 printable characters, saves the name and applies it without restarting. Existing lobbies may cache the earlier name until you leave and rejoin. Changing the name keeps the original save/content identity.

LAN is enabled by default. **Controls → LAN play** offers automatic discovery, Offline and an optional manual host IPv4 address. Completely close the game from Recents and reopen after changing the LAN mode. See [Android-to-Mac LAN instructions](../docs/LAN.md).

## Build from source

Build tools:

| Tool | Required version / notes |
| --- | --- |
| Git | Submodules and patch application |
| JDK | 17; set `JAVA_HOME` to this JDK |
| Android SDK | Platform 36, Platform Tools/ADB; accept SDK licenses |
| Android NDK | `28.2.13676358` (r28c), pinned by Gradle |
| CMake | `4.3.1`, pinned by Gradle |
| Ninja | Available to CMake |
| Python | 3 |
| Host C++ toolchain | Clang 18+ or Apple Clang 16+/Xcode 16+ for the native ReXGlue code generator; C++23 required |
| Gradle / Android plugin | Wrapper pins Gradle 8.13; Android Gradle Plugin 8.10.1 |
| Game executable | Verified USA `default.xex`, kept locally |

Use ample free disk space for submodules, native intermediates, generated code and APKs; plan for tens of GB. Build duration depends on the host CPU and available memory. This Android workflow was exercised on an Apple Silicon Mac; Linux hosts need equivalent Clang/build tools. Building the code generator and APK on the Android device itself is not supported by this workflow.

Clone and apply the complete portability patch:

```sh
git clone --recursive https://github.com/nahalewski/DBZBL-Android-LAN.git
cd DBZBL-Android-LAN
python3 scripts/apply_android_sdk_patch.py
```

Create ignored `android/local.properties` using absolute paths:

```properties
sdk.dir=/absolute/path/to/Android/sdk
cmake.dir=/absolute/path/to/cmake-installation
```

`cmake.dir` points to the installation containing `bin/cmake`, not the executable. Omit it if Android SDK already provides the required CMake version. Install NDK/platform/CMake packages using Android Studio's SDK Manager or `sdkmanager`.

### Prepare the local game executable

For an ISO, the repository's extractor verifies the USA executable before extracting:

```sh
python3 scripts/extract_android_game.py '/absolute/path/to/USA.iso'
```

For an extracted-game ZIP, unpack it locally and place the full contents in `game_data_root` at the repository root. `default.xex` must be directly inside that directory, with `LONG2DATA` beside it. Verify:

```sh
shasum -a 1 game_data_root/default.xex
```

On Linux, use `sha1sum` instead. The supported hash is listed above. Game data and generated code remain ignored by Git and are not copied as raw assets into the APK.

### Generate C++ on the build computer

Build the host tool with the host Clang compiler, **not the Android NDK compiler**. On Mac, the following explicit compilers select the supported Apple toolchain:

```sh
cmake -S thirdparty/rexglue-sdk -B out/host -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_PERF_COUNTERS=OFF
cmake --build out/host --target rexglue --parallel 6
thirdparty/rexglue-sdk/out/mac-arm64/rexglue codegen burstlimit_manifest.toml
```

On Linux, use `clang`/`clang++` version 18+ and the host output under `thirdparty/rexglue-sdk/out/linux-amd64/` or `linux-arm64/`, as appropriate. Code generation creates ignored `generated/default/sources.cmake` and game C++ from your own executable. The Android game build will stop with an explanation if this output is missing.

### Build and install the game APK

From the repository root, with JDK 17 selected:

```sh
./android/gradlew -p android assembleGameDebug
python3 scripts/check_android_apk.py \
  android/app/build/outputs/apk/game/debug/app-game-debug.apk
adb -s DEVICE_SERIAL install -r \
  android/app/build/outputs/apk/game/debug/app-game-debug.apk
```

Package: **`org.burstlimit.android`**. Launcher: **`org.burstlimit.android.GameSetupActivity`**. The debug APK is signed with the build computer's Android debug key. A distributable release should use your own maintained signing key; no signing credentials are supplied here. `assembleGameRelease` does not configure release signing automatically.

To transfer your own archive, choose either an ISO or an extracted-game ZIP:

```sh
adb -s DEVICE_SERIAL push '/absolute/path/to/USA.iso' /sdcard/Download/DBZ-Burst-Limit-USA.iso
# Or:
adb -s DEVICE_SERIAL push '/absolute/path/to/USA-game.zip' /sdcard/Download/DBZ-Burst-Limit-USA.zip
adb -s DEVICE_SERIAL shell am start -n org.burstlimit.android/.GameSetupActivity
```

Choose the transferred file in the first-launch picker. Use an explicit serial whenever multiple devices are connected. Local USB installation is also supported.

### Diagnostic build without game files

The probe exercises native threading, memory, Vulkan features, texture fixtures and virtual controller integration. It does not establish real gameplay compatibility:

```sh
./android/gradlew -p android assembleProbeDebug
adb -s DEVICE_SERIAL install -r \
  android/app/build/outputs/apk/probe/debug/app-probe-debug.apk
python3 scripts/android_device.py --serial DEVICE_SERIAL \
  --apk android/app/build/outputs/apk/probe/debug/app-probe-debug.apk \
  --launch --capture out/my-device-probe
```

Probe package: `org.burstlimit.android.probe`. It does not need an ISO or generated guest code.

## Wireless debugging

On the device, enable Developer options and Wireless debugging. Keep the device unlocked and connected to the same network as the build computer. Pair using **Pair device with pairing code**:

```sh
adb pair DEVICE_IP:PAIRING_PORT
adb connect DEVICE_IP:CONNECTION_PORT
adb devices -l
```

The pairing port and connection port are different. Connection ports can change when wireless debugging restarts. Accept Android's debugging authorization prompt. If ADB lists `unauthorized`, authorization must be completed on the device before deployment.

For an already extracted local game folder, the deployment helper can install, copy and capture evidence:

```sh
python3 scripts/android_device.py --serial DEVICE_IP:CONNECTION_PORT \
  --package org.burstlimit.android \
  --apk android/app/build/outputs/apk/game/debug/app-game-debug.apk \
  --game-data game_data_root --launch --capture out/my-device-game
```

A separate `--verify-manifest` accepts a local relative-path SHA-256 JSON manifest. It checks transfer integrity, not rendering or gameplay.

## Checks and troubleshooting

```sh
python3 -m unittest discover -s scripts -p test_extract_android_game.py -v
./android/gradlew -p android assembleGameDebugAndroidTest
adb -s DEVICE_SERIAL install -r \
  android/app/build/outputs/apk/androidTest/game/debug/app-game-debug-androidTest.apk
adb -s DEVICE_SERIAL shell am instrument -w \
  org.burstlimit.android.test/org.burstlimit.android.ControlsInstrumentation
```

The device runner reports controls and LAN results; its optional importer fixtures require a locally supplied USA executable. Host importer regression checks use `tests/GameDataImportProbe.java`, the production `GameDataImporter.java`, and `scripts/test_mac_game_import.py --android-java`. No game fixtures are published.

- **Wrong region/incomplete archive:** use the supported executable, finish the download, and unpack outer 7z/RAR/ISO-in-ZIP wrappers.
- **Import cannot read the ISO:** put it in local Downloads using a seekable provider.
- **Update signature mismatch:** use the original build/signing key. Uninstalling removes app data; back it up first.
- **Black rendering:** keep default compatibility settings, including `vulkan_dynamic_rendering=false`, and avoid diagnostic white shaders/readback settings. Current Samsung battle coverage remains incomplete.
- **Black screen after returning from another app:** the SDL/Vulkan surface replacement fix is included and was checked on Pixel 9 Pro Fold. Rotation/folding or backgrounding during an actual fight still requires broader testing.
- **Cannot find a LAN room:** follow [LAN troubleshooting](../docs/LAN.md). ADB wireless access is separate from the game's network connection.

The native libraries pass the 16 KB ELF alignment check. Actual execution on a device using 16 KB pages is still untested.
