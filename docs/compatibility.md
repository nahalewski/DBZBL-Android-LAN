# Compatibility and validation

Status from local testing on 2026-10-09. Test results are specific to the device and build used; they do not certify every model using the same GPU family.

| Platform | Hardware | Verified scope | Remaining coverage |
| --- | --- | --- | --- |
| Android | Pixel Fold, first generation; Mali-G710 | USA startup; touch controls; USB Razer Kishi V2 Pro takeover and return; a bounded live CPU battle; two complete 60-second LAN rounds against native Mac with movement/attacks in both directions; latest setup APK startup and game transfer integrity | Full campaign, long sessions, complete audio/save/resume coverage |
| Android | Pixel 9 Pro Fold; Mali-G715 | USA startup and menus; corrected BC2/BC3 textures; all 16 texture fixtures with zero pixel mismatches; repeated Home/Settings/Recents returns recreated the Vulkan surface while preserving the process; latest setup APK startup and file transfer integrity | Comprehensive current-build battles, folding/background transitions during fights, extended performance |
| Android | Galaxy Tab S11 Ultra, SM-X930; Mali-G925 Immortalis | Runtime/GPU/input checks; host session and prior character-selection attempts; native first-run setup selected the real USA extracted-game ZIP, copied it and verified all ten installed file hashes; intro/startup on latest APK | Current full gameplay/LAN. Earlier builds reached battle with black geometry/frames and later disconnected; latest startup is not proof that this is resolved on the tablet. |
| Android | Razer Edge 5G | Deployment/testing skipped | Device-specific compatibility remains unverified |
| Android | Other Mali, Adreno and other Vulkan GPUs | Feature detection and compatibility paths are present | Actual device testing required |
| Mac | Apple Silicon M1 Pro, macOS 27.2 | Native ARM64 app; first-run USA ZIP setup; main menus; bounded offline CPU fight; keyboard navigation/attacks; two complete LAN rounds with first-generation Pixel Fold | Full campaign, longer crash/stress checks, physical Mac gamepad, other chips/OS versions |

## Included platform fixes

- ARM64/POSIX runtime portability: guest memory, fibers, synchronization, timers, threading, sockets and SDK code generation compatibility.
- Vulkan texture fetch exponent decoding, shader cache invalidation and BC2/BC3 fallback interpolation. This corrected black/dim textures and corrupted menu text in the tested paths.
- Compute vertex memory export fallback on Mali devices lacking vertex-stage stores/atomics. Some aliased-index and tessellated export cases are still rejected.
- Android background/foreground handling retires the old presenter and acquires the replacement Android native window/swapchain. Intro/menu recovery was tested on Pixel 9 Pro Fold; fight-time and folding transitions need broader tests.
- Wildcard UDP binding preserves the game's self-addressed loopback match-start notification.
- LAN discovery includes gamertags and match rules and filters full/private/deleted/incompatible/self rooms.
- Mac MoltenVK drawable lifetime and runtime staging corrections.

Use `vulkan_dynamic_rendering=false`, 1×/720p, 30 FPS and standard shaders as the starting configuration. Development diagnostic white-shader variants, draw census and resolve readback are off by default and should stay off during ordinary play.

## Import and build checks

Production importer regression checks cover five Xbox ISO partition offsets, extracted-game ZIP layouts, wrong executables, truncated inputs, invalid bounds, cycles, traversal, path collisions, cancellation cleanup and preserving occupied destinations. Android device tests exercise descriptor-based ISO reads, sequential ZIP reads and interrupted setup cleanup. A real USA ZIP was selected through the Android system picker and all resulting game files matched the source hashes.

Android native libraries pass the **16 KB ELF alignment** checker. Tested Android devices used **4 KB** pages; execution on a 16 KB device remains untested. Alignment is a packaging property, not proof of runtime behavior on such a device.

## Limits

The ports are development builds. Bounded successful tests do not establish crash-free operation, stable sustained frame rate, full campaign completion or reliable audio on every platform. Earlier Mac stress checks encountered MoltenVK/rendering failures. Samsung current battle coverage remains incomplete. Physical Bluetooth gamepads, an attached Mac controller and additional GPU families require tests.

Local screenshots, logs, deployment reports, device addresses, game files and saves are deliberately outside the published source. The source includes repeatable diagnostic/test programs; importer tests requiring the supported executable use your own local copy.
