#include <cstdint>
#include <mutex>
#include <string_view>

#include <rex/cvar.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>

REXCVAR_DEFINE_BOOL(patch_60fps, false, "Patches",
                    "Enable the 60 FPS patch with pause and match-exit fixes.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(online_fast_tick, true, "Patches",
                    "Online: run the match frame driver every frame instead of every 4th.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(online_input_delay_test, true, "Patches",
                    "Online: lower the input buffer threshold at 0x82293A40 from 6 to 2.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

// Guest frame interval (vblanks per game tick). The game writes 2 (30 FPS)
// through sub_82218940; the 60 FPS patch forces 1. The pause/match-quit code
// only runs when the tick counter at [0x825205E8]+3228 is non-zero, which
// never happens with interval 1, so the Skip hooks in the generated code must
// stay in place or START/pause locks up.
constexpr uint32_t kFpsCapAddress = 0x826DE600;
constexpr uint32_t kFpsCap30 = 2;
constexpr uint32_t kFpsCap60 = 1;

std::mutex g_patch_mutex;
bool g_fps_cap_applied = false;

void Apply60FpsDataPatch() {
  auto* runtime = rex::Runtime::instance();
  if (!runtime) {
    return;
  }

  auto* memory = runtime->memory();
  if (!memory) {
    return;
  }

  auto* fps_cap = memory->TranslateVirtual<uint8_t*>(kFpsCapAddress);
  if (!fps_cap) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_patch_mutex);

  const uint32_t current = rex::memory::load_and_swap<uint32_t>(fps_cap);

  if (REXCVAR_GET(patch_60fps)) {
    // Only override the game's own 30 FPS value; leave 0 (not initialized
    // yet) and any other mode the game picks alone.
    if (current == kFpsCap30) {
      rex::memory::store_and_swap<uint32_t>(fps_cap, kFpsCap60);
    }
    g_fps_cap_applied = true;
  } else if (g_fps_cap_applied) {
    // Always restore the game's real default instead of a value captured
    // at an arbitrary time (it could have been 0 before the game set it).
    if (current == kFpsCap60) {
      rex::memory::store_and_swap<uint32_t>(fps_cap, kFpsCap30);
    }
    g_fps_cap_applied = false;
  }
}

struct PatchCvarCallbacks {
  PatchCvarCallbacks() {
    rex::cvar::RegisterChangeCallback(
        "patch_60fps",
        [](std::string_view, std::string_view) { Apply60FpsDataPatch(); });
  }

  ~PatchCvarCallbacks() {
    rex::cvar::UnregisterChangeCallbacks("patch_60fps");
  }
};

PatchCvarCallbacks g_patch_cvar_callbacks;

bool Is60FpsEnabled() {
  Apply60FpsDataPatch();
  return REXCVAR_GET(patch_60fps);
}

}  // namespace

// Mid-asm hooks, wired up in burstlimit_manifest.toml.

// li r3,2 before bl sub_82218940: force frame interval 1.
void BurstLimit60FpsForceCap(PPCRegister& r3) {
  if (Is60FpsEnabled()) {
    r3.u64 = 1;
  }
}

// beq on "tick counter == 0" in the pause / match-quit paths.
void BurstLimit60FpsSkipTickGate(PPCCRRegister& cr6) {
  if (Is60FpsEnabled()) {
    cr6.eq = 0;
  }
}

// li r4,3 before bl sub_82122310 in the online frame driver: task sleep ticks.
void BurstLimitOnlineDriverSleep(PPCRegister& r4) {
  if (REXCVAR_GET(online_fast_tick)) {
    r4.u64 = 0;
  }
}

// cmpwi cr6,r11,6: redo the compare against 2 for the online latency test.
void BurstLimitOnlineInputDelay(PPCRegister& r11, PPCCRRegister& cr6) {
  if (REXCVAR_GET(online_input_delay_test)) {
    const int32_t value = r11.s32;
    cr6.lt = value < 2;
    cr6.gt = value > 2;
    cr6.eq = value == 2;
  }
}
