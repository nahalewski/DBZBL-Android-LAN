#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "burstlimit_android_input.h"
#include "lan_probe.h"
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <fstream>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/net/lan.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/xmemory.h>
#include <rex/thread.h>
#include <rex/thread/fiber.h>
#include <rex/thread/timer_queue.h>
#include <rex/ui/vulkan/provider.h>
#include <string>
#include <thread>
#include <unistd.h>

REXCVAR_DEFINE_STRING(probe_udp_peer, "", "Diagnostics", "Optional IPv4 UDP echo test peer.");
REXCVAR_DEFINE_STRING(probe_lan_version, "", "Diagnostics", "LAN-only probe using this game compatibility version.");

namespace {
std::string report;
void Result(const char *name, bool ok) {
  report += std::string(name) + ": " + (ok ? "PASS" : "FAIL") + "\n";
  __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
                      "BurstLimitProbe", "%s: %s", name, ok ? "PASS" : "FAIL");
}
struct FiberTest {
  rex::thread::Fiber *parent;
  int step = 0;
};
void FiberEntry(void *data) {
  auto *test = static_cast<FiberTest *>(data);
  volatile double value = 3.141592653589793;
  test->step = 1;
  rex::thread::Fiber::SwitchTo(test->parent);
  test->step = std::abs(value - 3.141592653589793) < 1e-12 ? 2 : -1;
  rex::thread::Fiber::SwitchTo(test->parent);
}
} // namespace

int main(int argc, char **argv) {
  rex::cvar::Init(argc, argv);
  // Android's app directories are writable; the APK's library directory is not.
  const char *storage = SDL_GetAndroidExternalStoragePath();
  if (!storage)
    storage = SDL_GetAndroidInternalStoragePath();
  if (storage)
    chdir(storage);
  rex::InitLoggingEarly();
  rex::cvar::SetFlagByName("online_mode", "direct");
  if (!REXCVAR_GET(probe_lan_version).empty()) {
    // Fast network-only mode: exercises exactly the production discovery
    // implementation without rerunning unrelated GPU/runtime checks.
    unsetenv("REX_XNET_SEARCH_IP");
    unsetenv("REX_LAN_DISABLED");
    rex::net::online::SetGameVersion(REXCVAR_GET(probe_lan_version));
    rex::net::lan::ConfigureLocalAddress();
    const auto rooms = rex::net::lan::Search(5, {});
    for (const auto& room : rooms) {
      report += "Discovered host: " + room.host_name + " address=" + std::to_string(room.host_ip) + "\n";
      __android_log_print(ANDROID_LOG_INFO, "BurstLimitLAN", "Auto-discovered %s (%u)", room.host_name.c_str(), room.host_ip);
    }
    Result("Automatic LAN host discovery without IP entry", !rooms.empty());
    const auto saved_storage = rex::net::online::StorageProfileName();
    Result("Live gamertag preserves storage identity",
           rex::net::online::SetPlayerName("PixelTest") && rex::net::online::PlayerName() == "PixelTest" &&
           rex::net::online::StorageProfileName() == saved_storage &&
           !rex::net::online::SetPlayerName("bad\nname"));
    std::ofstream("runtime-probe.txt") << report;
    return rooms.empty() ? 1 : 0;
  }
  Result("Xbox socket options, nonblocking UDP and byte order", burstlimit::android::ProbeSocketOptions());
  const auto peer = REXCVAR_GET(probe_udp_peer);
  if (!peer.empty()) {
    std::string details;
    bool ok = burstlimit::android::ProbeUdpEcho(peer, details);
    report += details;
    Result("LAN UDP game-port round trips", ok);
  }
  rex::memory::AndroidInitialize();
  rex::thread::AndroidInitialize();
  Result("SDL video/audio/gamepad",
         SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD));
  {
    using namespace burstlimit::android;
    bool ok = EnableTouchController(true);
    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* pad = nullptr;
    for (int i = 0; ids && i < count; ++i) {
      const char* name = SDL_GetGamepadNameForID(ids[i]);
      if (name && std::string(name) == "Burst Limit touch controller")
        pad = SDL_OpenGamepad(ids[i]);
    }
    SDL_free(ids);
    ok = ok && pad;
    if (pad) {
      SDL_UpdateJoysticks();
      ok = ok && SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0 &&
           SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == 0;
      for (int button = 0; button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; ++button) {
        SetTouchControllerState(uint32_t{1} << button, 1, -1, -1, 1, 1, .5f);
        SDL_UpdateJoysticks();
        for (int other = 0; other <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; ++other)
          ok = ok && SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(other)) ==
                         (other == button);
        ok = ok && SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) == 32767 &&
             SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) == -32768 &&
             SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) == -32768 &&
             SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) == 32767 &&
             SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 32767 &&
             std::abs(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)-16383) <= 1;
      }
      SetTouchControllerState(0, 0, 0, 0, 0, 0, 0);
      SDL_UpdateJoysticks();
      for (int button = 0; button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; ++button)
        ok = ok && !SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button));
      for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
        ok = ok && SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(axis)) == 0;
      SDL_CloseGamepad(pad);
    }
    EnableTouchController(false);
    int remaining = 0;
    auto* remaining_ids = SDL_GetGamepads(&remaining);
    for (int i = 0; remaining_ids && i < remaining; ++i) {
      const char* name = SDL_GetGamepadNameForID(remaining_ids[i]);
      ok = ok && (!name || std::string(name) != "Burst Limit touch controller");
    }
    SDL_free(remaining_ids);
    ok = ok && EnableTouchController(true);
    EnableTouchController(false);
    Result("Touch gamepad buttons, sticks, triggers, release and reconnect", ok);
  }
  auto *window = SDL_CreateWindow("Burst Limit runtime diagnostics", 1280, 720,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  Result("Android native window", window != nullptr);

  FiberTest test{rex::thread::Fiber::ConvertCurrentThread()};
  auto *child = rex::thread::Fiber::Create(65536, FiberEntry, &test);
  bool fibers_ok = test.parent && child;
  if (fibers_ok) {
    rex::thread::Fiber::SwitchTo(child);
    fibers_ok = test.step == 1;
    rex::thread::Fiber::SwitchTo(child);
    fibers_ok = fibers_ok && test.step == 2;
    child->Destroy();
    test.parent->Destroy();
  }
  Result("ARM64 fiber suspend/resume", fibers_ok);
  {
    // Exercise the game's shared-event monitor: signal-and-wait releases
    // a mutant, wakes the other participant, then reacquires the mutant.
    // A participant must not consume the wakeup assigned to its peer.
    using namespace std::chrono_literals;
    auto mutex = rex::thread::Mutant::Create(false);
    auto changed = rex::thread::Event::CreateAutoResetEvent(false);
    std::atomic<bool> ok{true};
    int state = 0;
    auto acquire = [&] {
      return rex::thread::Wait(mutex.get(), false, 2s) ==
             rex::thread::WaitResult::kSuccess;
    };
    auto await = [&](int wanted) {
      while (state != wanted && ok.load()) {
        auto result = rex::thread::SignalAndWait(mutex.get(), changed.get(), true, 2s);
        if (!acquire()) { ok = false; return false; }
        if (result != rex::thread::WaitResult::kSuccess) { ok = false; return false; }
      }
      return ok.load();
    };
    std::thread worker([&] {
      for (int i = 0; i < 1000 && ok.load(); ++i) {
        if (!acquire()) { ok = false; break; }
        if (await(1)) { state = 0; changed->Set(); }
        mutex->Release();
      }
    });
    for (int i = 0; i < 1000 && ok.load(); ++i) {
      if (!acquire()) { ok = false; break; }
      state = 1;
      changed->Set();
      await(0);
      mutex->Release();
    }
    worker.join();
    Result("Signal-and-wait shared-event monitor (1000 jobs)", ok.load());
  }
  {
    using namespace std::chrono_literals;
    auto event = rex::thread::Event::CreateAutoResetEvent(false);
    int callbacks = 0;
    rex::thread::Thread::GetCurrentThread()->QueueUserCallback([&] {
      ++callbacks;
      event->Set();
    });
    bool ok = rex::thread::Wait(event.get(), true, 1s) ==
              rex::thread::WaitResult::kUserCallback && callbacks == 1;
    ok = ok && rex::thread::Wait(event.get(), false, 0ms) ==
                   rex::thread::WaitResult::kSuccess;
    ok = ok && rex::thread::Wait(event.get(), false, 0ms) ==
                   rex::thread::WaitResult::kTimeout;
    ok = ok && rex::thread::SignalAndWait(event.get(), event.get(), true, 1s) ==
                   rex::thread::WaitResult::kSuccess;
    ok = ok && rex::thread::Wait(event.get(), false, 0ms) ==
                   rex::thread::WaitResult::kTimeout;
    auto manual = rex::thread::Event::CreateManualResetEvent(true);
    ok = ok && rex::thread::Wait(manual.get(), true, 0ms) ==
                   rex::thread::WaitResult::kSuccess;
    ok = ok && rex::thread::Wait(manual.get(), false, 0ms) ==
                   rex::thread::WaitResult::kSuccess;
    manual->Reset();
    ok = ok && rex::thread::Wait(manual.get(), true, 0ms) ==
                   rex::thread::WaitResult::kTimeout;
    Result("Event APC delivery, notification retention and reset", ok);
  }
  {
    std::atomic<int> calls{0};
    auto timer = rex::thread::QueueTimerRecurring(
        [](void *data) { ++*static_cast<std::atomic<int> *>(data); }, &calls,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(20),
        std::chrono::milliseconds(20));
    for (int i = 0; i < 100 && calls.load() < 2; ++i)
      SDL_Delay(5);
    bool timer_ok = calls.load() >= 2;
    if (auto item = timer.lock())
      item->Disarm();
    else
      timer_ok = false;
    int cancelled_at = calls.load();
    SDL_Delay(60);
    Result("Recurring timer and cancellation",
           timer_ok && calls.load() == cancelled_at);
  }
  {
    rex::memory::Memory memory;
    Result("Guest address-space mapping", memory.Initialize());
  }
  // Baseline Vulkan is tested separately from the full Xenos requirements.
  auto baseline = rex::ui::vulkan::VulkanProvider::Create(false, true);
  Result("Vulkan logical device", baseline != nullptr);
  if (baseline) {
    auto *instance = baseline->vulkan_instance();
    auto *device = baseline->vulkan_device();
    const auto &properties = device->properties();
    VkPhysicalDeviceFeatures supported{};
    instance->functions().vkGetPhysicalDeviceFeatures(device->physical_device(),
                                                      &supported);
    report += std::string("GPU: ") + properties.deviceName + "\n";
    for (auto [name, supported] :
         {std::pair{"independentBlend", supported.independentBlend},
          std::pair{"vertexPipelineStoresAndAtomics",
                    supported.vertexPipelineStoresAndAtomics},
          std::pair{"fragmentStoresAndAtomics",
                    supported.fragmentStoresAndAtomics},
          std::pair{"geometryShader", supported.geometryShader},
          std::pair{"tessellationShader", supported.tessellationShader},
          std::pair{"fillModeNonSolid", supported.fillModeNonSolid}})
      report += std::string(name) + ": " + (supported ? "yes" : "no") + "\n";
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    auto *native = window
                       ? static_cast<ANativeWindow *>(SDL_GetPointerProperty(
                             SDL_GetWindowProperties(window),
                             SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr))
                       : nullptr;
    VkAndroidSurfaceCreateInfoKHR create{
        VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    create.window = native;
    bool surface_ok =
        native && instance->functions().vkCreateAndroidSurfaceKHR &&
        instance->functions().vkCreateAndroidSurfaceKHR(
            instance->instance(), &create, nullptr, &surface) == VK_SUCCESS;
    if (surface_ok) {
      uint32_t count = 0;
      instance->functions().vkGetPhysicalDeviceSurfaceFormatsKHR(
          device->physical_device(), surface, &count, nullptr);
      surface_ok = count > 0;
      instance->functions().vkDestroySurfaceKHR(instance->instance(), surface,
                                                nullptr);
    }
    Result("Android Vulkan surface formats", surface_ok);
  }
  baseline.reset();
  // Request the same GPU feature set used by the actual game renderer.
  auto provider = rex::ui::vulkan::VulkanProvider::Create(true, true);
  Result("ReXGlue Vulkan GPU requirements", provider != nullptr);
  if (provider) {
    const auto& features = provider->vulkan_device()->properties();
    report += std::string("Fragment shader interlock: ") +
              (features.fragmentShaderPixelInterlock || features.fragmentShaderSampleInterlock
                   ? "available" : "unavailable") + "\n";
    auto *plugin = dlopen("librexgpu-xenos.so", RTLD_NOW);
    using Test = bool (*)(const rex::ui::vulkan::VulkanDevice *);
    auto test_compute =
        plugin ? reinterpret_cast<Test>(
                     dlsym(plugin, "burstlimit_test_compute_memexport"))
               : nullptr;
    Result("Mali compute memory export readback",
           test_compute && test_compute(provider->vulkan_device()));
    auto test_texture = plugin ? reinterpret_cast<Test>(
        dlsym(plugin, "burstlimit_test_texture_decode")) : nullptr;
    Result("Bundled BC1/BC2/BC3/BC4 texture decode readback",
           test_texture && test_texture(provider->vulkan_device()));
    if (plugin)
      dlclose(plugin);
  }
  auto gpu = rex::system::LoadGpuPlugin("xenos", "vulkan");
  Result("Xenos plugin loading", gpu != nullptr);
  if (gpu) {
    Result("Xenos presenter initialization",
           gpu->SetupPresentation(nullptr) == 0);
    // Exercise the real guest GPU worker and kernel host threads without
    // loading a game image or supplying invented guest function mappings.
    provider.reset();
    rex::Runtime runtime({}, std::string(storage ? storage : ".") +
                                 "/probe_user_data");
    rex::cvar::SetFlagByName("vulkan_readback_memexport", "true");
    // Assertions compare this draw's CPU memory immediately. The optional
    // fast path deliberately returns a completed previous draw's readback.
    rex::cvar::SetFlagByName("readback_memexport_fast", "false");
    rex::cvar::SetFlagByName("async_shader_compilation", "false");
    rex::RuntimeConfig config;
    config.graphics = std::move(gpu);
    bool runtime_ok = runtime.Setup(std::move(config)) == 0;
    Result("Guest kernel and GPU setup", runtime_ok);
    if (runtime_ok) {
      auto *plugin = dlopen("librexgpu-xenos.so", RTLD_NOW);
      using TestWorker = bool (*)(rex::system::IGraphicsSystem *);
      auto test_worker = plugin ? reinterpret_cast<TestWorker>(dlsym(
                                      plugin, "burstlimit_test_gpu_worker"))
                                : nullptr;
      Result("Xenos GPU command worker ready",
             test_worker && test_worker(runtime.graphics_system()));
      auto test_draw = plugin ? reinterpret_cast<TestWorker>(dlsym(
                                    plugin, "burstlimit_test_memexport_draw"))
                              : nullptr;
      Result("Xenos draw compute export readback",
             test_draw && test_draw(runtime.graphics_system()));
      if (plugin)
        dlclose(plugin);
    }
    runtime.Shutdown();
  }
  report += "Page size: " + std::to_string(sysconf(_SC_PAGESIZE)) + "\n";
  report += "Runtime probe only. Game executable/data are not included.\n";
  if (storage)
    std::ofstream(std::string(storage) + "/runtime-probe.txt") << report;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
                           "Burst Limit Android runtime test", report.c_str(),
                           window);
  // Stay alive for surface changes, controller hotplug and adb inspection.
  bool running = window != nullptr;
  while (running) {
    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, 100)) {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
      if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        auto *pad = SDL_OpenGamepad(event.gdevice.which);
        __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                            "Controller: %s",
                            pad ? SDL_GetGamepadName(pad) : SDL_GetError());
      }
      if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                            "Surface resized: %dx%d", event.window.data1,
                            event.window.data2);
    }
  }
  provider.reset();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
