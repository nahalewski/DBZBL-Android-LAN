// Free camera, and console commands to look through guest memory (used to
// find the game's camera).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/draw_overrides.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/ui/overlay/quick_menu.h>

REXCVAR_DEFINE_BOOL(free_camera, false, "Patches",
                    "Free camera for screenshots: left stick moves, right stick looks, LB/RB "
                    "down/up, LT/RT slower/faster, D-pad up/down zoom, Y back to the game's "
                    "view, B exits. The game doesn't get the controller meanwhile.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

// The camera manager is a static at 0x841B2B20 (pointed to by 0x841B2B10).
// Its active camera at +0x150 is what the game renders from: eye and target as
// x, y, z, 1 at +0x00 and +0x10, the vertical FOV in radians at +0x34, flags
// at +0x38 (bit 0 = changed). The camera mode writes the eye and target it
// wants at +0x10 and +0x20 of the manager, and sub_8216E850 eases the active
// camera toward them every frame.
constexpr uint32_t kManagerPointer = 0x841B2B10;
constexpr uint32_t kManagerEye = 0x10;
constexpr uint32_t kManagerTarget = 0x20;
constexpr uint32_t kActiveCamera = 0x150;
constexpr uint32_t kCameraEye = 0x00;
constexpr uint32_t kCameraTarget = 0x10;
constexpr uint32_t kCameraFov = 0x34;
constexpr uint32_t kCameraFlags = 0x38;

// X_INPUT_GAMEPAD_* bits.
constexpr uint16_t kPadUp = 0x0001;
constexpr uint16_t kPadDown = 0x0002;
constexpr uint16_t kPadLeft = 0x0004;
constexpr uint16_t kPadRight = 0x0008;
constexpr uint16_t kPadLeftShoulder = 0x0100;
constexpr uint16_t kPadRightShoulder = 0x0200;
constexpr uint16_t kPadA = 0x1000;
constexpr uint16_t kPadB = 0x2000;
constexpr uint16_t kPadY = 0x8000;

struct Vec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};

Vec3 ReadVec3(const uint8_t* p) {
  return {rex::memory::load_and_swap<float>(p), rex::memory::load_and_swap<float>(p + 4),
          rex::memory::load_and_swap<float>(p + 8)};
}

void WriteVec3(uint8_t* p, const Vec3& v) {
  rex::memory::store_and_swap<float>(p, v.x);
  rex::memory::store_and_swap<float>(p + 4, v.y);
  rex::memory::store_and_swap<float>(p + 8, v.z);
  rex::memory::store_and_swap<float>(p + 12, 1.0f);
}

struct PadInput {
  uint16_t buttons = 0;
  float left_x = 0.0f, left_y = 0.0f, right_x = 0.0f, right_y = 0.0f;
  float left_trigger = 0.0f, right_trigger = 0.0f;
};

float StickAxis(int16_t raw) {
  constexpr float kDeadzone = 0.2f;
  const float value = std::clamp(float(raw) / 32767.0f, -1.0f, 1.0f);
  if (std::fabs(value) < kDeadzone) {
    return 0.0f;
  }
  return (value - std::copysign(kDeadzone, value)) / (1.0f - kDeadzone);
}

rex::input::InputSystem* GetInputSystem() {
  auto* runtime = rex::Runtime::instance();
  return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
}

// All controllers, merged: the stick pushed the furthest wins.
PadInput ReadPad(rex::input::InputSystem* input) {
  using rex::X_RESULT;
  PadInput pad;
  float left_distance = 0.0f, right_distance = 0.0f;
  for (uint32_t user = 0; user < rex::input::kMaxGuestUsers; ++user) {
    rex::input::X_INPUT_STATE state = {};
    if (input->GetStateForUI(user, &state) != X_ERROR_SUCCESS) {
      continue;
    }
    const auto& gamepad = state.gamepad;
    pad.buttons |= static_cast<uint16_t>(gamepad.buttons);
    const float lx = StickAxis(gamepad.thumb_lx), ly = StickAxis(gamepad.thumb_ly);
    const float rx = StickAxis(gamepad.thumb_rx), ry = StickAxis(gamepad.thumb_ry);
    if (std::fabs(lx) + std::fabs(ly) > left_distance) {
      left_distance = std::fabs(lx) + std::fabs(ly);
      pad.left_x = lx;
      pad.left_y = ly;
    }
    if (std::fabs(rx) + std::fabs(ry) > right_distance) {
      right_distance = std::fabs(rx) + std::fabs(ry);
      pad.right_x = rx;
      pad.right_y = ry;
    }
    pad.left_trigger = std::max(pad.left_trigger, float(gamepad.left_trigger) / 255.0f);
    pad.right_trigger = std::max(pad.right_trigger, float(gamepad.right_trigger) / 255.0f);
  }
  return pad;
}

// Game thread only (the camera update hook).
struct FreeCamera {
  bool active = false;
  bool blocking_input = false;
  bool show_hud = false;
  bool pad_seen = false;
  uint16_t last_buttons = 0;
  uint16_t exit_buttons = 0;
  Vec3 position;
  float yaw = 0.0f;    // 0 = looking down -Z, positive turns right (+X).
  float pitch = 0.0f;  // Positive looks up.
  float roll = 0.0f;   // Turns the picture (the GPU does it).
  float fov = 0.6f;
  std::chrono::steady_clock::time_point last_update;
} g_free_camera;

Vec3 Forward(float yaw, float pitch) {
  return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

// Points the free camera like the game's camera from `eye` to `target`.
void AimFreeCamera(const Vec3& eye, const Vec3& target) {
  FreeCamera& camera = g_free_camera;
  camera.position = eye;
  Vec3 direction{target.x - eye.x, target.y - eye.y, target.z - eye.z};
  const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                 direction.z * direction.z);
  if (length > 1e-4f) {
    camera.pitch = std::asin(std::clamp(direction.y / length, -1.0f, 1.0f));
    camera.yaw = std::atan2(direction.x, -direction.z);
  }
}

void StopFreeCamera() {
  FreeCamera& camera = g_free_camera;
  rex::graphics::SetHideHudDraws(false);
  rex::graphics::SetSceneProjectionRoll(0.0f);
  camera.roll = 0.0f;
  if (camera.blocking_input) {
    if (auto* input = GetInputSystem()) {
      input->RemoveUIInputBlocker();
    }
    camera.blocking_input = false;
  }
  camera.active = false;
}

}  // namespace

// Mid-asm hook in the main loop (sub_822197D0, 0x822198F8), right after the
// frame's update - the normal one, which moves the game's camera, or the
// paused one - and before the scene is drawn.
void BurstLimitCameraFrame(PPCRegister& r30) {
  (void)r30;
  FreeCamera& camera = g_free_camera;
  if (!REXCVAR_GET(free_camera)) {
    if (camera.active) {
      StopFreeCamera();
    }
    return;
  }
  auto* runtime = rex::Runtime::instance();
  auto* memory = runtime ? runtime->memory() : nullptr;
  auto* input = GetInputSystem();
  if (!memory || !input) {
    return;
  }
  const uint32_t manager_address =
      rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual<uint8_t*>(kManagerPointer));
  if (!manager_address) {
    return;
  }
  uint8_t* manager = memory->TranslateVirtual<uint8_t*>(manager_address);
  uint8_t* active = manager + kActiveCamera;

  const auto now = std::chrono::steady_clock::now();
  if (!camera.active) {
    // Start where the game's camera is.
    AimFreeCamera(ReadVec3(active + kCameraEye), ReadVec3(active + kCameraTarget));
    camera.fov = rex::memory::load_and_swap<float>(active + kCameraFov);
    camera.active = true;
    camera.show_hud = false;
    rex::graphics::SetHideHudDraws(true);
    camera.pad_seen = false;
    camera.exit_buttons = 0;
    camera.last_update = now;
    input->AddUIInputBlocker();
    camera.blocking_input = true;
  }
  const float dt = std::clamp(
      std::chrono::duration<float>(now - camera.last_update).count(), 0.0f, 0.1f);
  camera.last_update = now;

  // The settings menu has the controllers while it's open.
  PadInput pad;
  if (!rex::ui::QuickMenuDialog::IsOpen()) {
    pad = ReadPad(input);
    if (!camera.pad_seen) {
      // What's held when the camera takes over isn't a press.
      camera.pad_seen = true;
      camera.last_buttons = pad.buttons;
    }
  } else {
    camera.pad_seen = false;
  }
  const uint16_t pressed = pad.buttons & ~camera.last_buttons;
  camera.last_buttons = pad.buttons;

  if (pressed & kPadB) {
    camera.exit_buttons |= kPadB;
  }
  // Exit once B is let go, so the game doesn't get the press.
  if (camera.exit_buttons && !(pad.buttons & camera.exit_buttons)) {
    rex::cvar::SetFlagByName("free_camera", "false");
    StopFreeCamera();
    return;
  }
  if (pressed & kPadY) {
    AimFreeCamera(ReadVec3(manager + kManagerEye), ReadVec3(manager + kManagerTarget));
    camera.roll = 0.0f;
  }

  // Roll.
  constexpr float kRollSpeed = 1.0f;  // Radians per second.
  if (pad.buttons & kPadLeft) {
    camera.roll += kRollSpeed * dt;
  }
  if (pad.buttons & kPadRight) {
    camera.roll -= kRollSpeed * dt;
  }
  camera.roll = std::clamp(camera.roll, -3.1416f, 3.1416f);
  rex::graphics::SetSceneProjectionRoll(camera.roll);
  if (pressed & kPadA) {
    camera.show_hud = !camera.show_hud;
    rex::graphics::SetHideHudDraws(!camera.show_hud);
  }

  // Look.
  constexpr float kLookSpeed = 1.8f;  // Radians per second.
  constexpr float kMaxPitch = 1.45f;
  camera.yaw += pad.right_x * kLookSpeed * dt;
  camera.pitch = std::clamp(camera.pitch + pad.right_y * kLookSpeed * dt, -kMaxPitch, kMaxPitch);

  // Move, relative to where the camera looks.
  float speed = 20.0f;  // Units per second (the fighters start 30 apart).
  speed *= 1.0f + 3.0f * pad.right_trigger;
  speed *= 1.0f - 0.75f * pad.left_trigger;
  const Vec3 forward = Forward(camera.yaw, camera.pitch);
  const Vec3 right{std::cos(camera.yaw), 0.0f, std::sin(camera.yaw)};
  float rise = 0.0f;
  if (pad.buttons & kPadRightShoulder) {
    rise += 1.0f;
  }
  if (pad.buttons & kPadLeftShoulder) {
    rise -= 1.0f;
  }
  camera.position.x += (forward.x * pad.left_y + right.x * pad.left_x) * speed * dt;
  camera.position.y += (forward.y * pad.left_y + rise) * speed * dt;
  camera.position.z += (forward.z * pad.left_y + right.z * pad.left_x) * speed * dt;

  // Zoom.
  if (pad.buttons & kPadUp) {
    camera.fov -= 0.6f * dt;
  }
  if (pad.buttons & kPadDown) {
    camera.fov += 0.6f * dt;
  }
  camera.fov = std::clamp(camera.fov, 0.1f, 1.6f);

  const Vec3 target{camera.position.x + forward.x * 10.0f, camera.position.y + forward.y * 10.0f,
                    camera.position.z + forward.z * 10.0f};
  WriteVec3(active + kCameraEye, camera.position);
  WriteVec3(active + kCameraTarget, target);
  rex::memory::store_and_swap<float>(active + kCameraFov, camera.fov);
  rex::memory::store_and_swap<uint32_t>(
      active + kCameraFlags, rex::memory::load_and_swap<uint32_t>(active + kCameraFlags) | 1u);
}

namespace {

std::vector<std::string> SplitArgs(std::string_view args) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start < args.size()) {
    while (start < args.size() && args[start] == ' ') {
      ++start;
    }
    size_t end = start;
    while (end < args.size() && args[end] != ' ') {
      ++end;
    }
    if (end > start) {
      parts.emplace_back(args.substr(start, end - start));
    }
    start = end;
  }
  return parts;
}

// Calls `visit(guest_address, host_pointer, size)` for each committed range of
// guest memory.
template <typename Visit>
void ForEachCommittedRange(Visit&& visit) {
  auto* runtime = rex::Runtime::instance();
  auto* memory = runtime ? runtime->memory() : nullptr;
  if (!memory) {
    return;
  }
  uint64_t address = 0x10000;
  while (address < 0x100000000ull) {
    auto* heap = memory->LookupHeap(uint32_t(address));
    if (!heap) {
      address += 0x10000;
      continue;
    }
    rex::memory::HeapAllocationInfo info = {};
    if (!heap->QueryRegionInfo(uint32_t(address), &info) || !info.region_size) {
      address += heap->page_size();
      continue;
    }
    const uint64_t region_end = uint64_t(info.base_address) + info.region_size;
    if ((info.state & rex::memory::kMemoryAllocationCommit) &&
        (info.protect & rex::memory::kMemoryProtectRead)) {
      visit(uint32_t(address), memory->TranslateVirtual<const uint8_t*>(uint32_t(address)),
            size_t(region_end - address));
    }
    address = std::max(region_end, address + 4);
  }
}

constexpr size_t kMaxMatches = 40;

// mem_find_words: guest addresses where these big-endian 32-bit words (hex)
// follow each other.
void MemFindWords(std::string_view args) {
  std::vector<uint32_t> words;
  for (const std::string& part : SplitArgs(args)) {
    words.push_back(uint32_t(std::strtoul(part.c_str(), nullptr, 16)));
  }
  if (words.empty()) {
    REXLOG_WARN("mem_find_words: usage: mem_find_words <hex word> [<hex word> ...]");
    return;
  }
  size_t matches = 0;
  ForEachCommittedRange([&](uint32_t address, const uint8_t* host, size_t size) {
    const size_t needed = words.size() * 4;
    for (size_t offset = 0; offset + needed <= size && matches < kMaxMatches; offset += 4) {
      bool match = true;
      for (size_t i = 0; i < words.size(); ++i) {
        if (rex::memory::load_and_swap<uint32_t>(host + offset + i * 4) != words[i]) {
          match = false;
          break;
        }
      }
      if (match) {
        REXLOG_WARN("mem_find_words: {:08X}", address + uint32_t(offset));
        ++matches;
      }
    }
  });
  REXLOG_WARN("mem_find_words: {} match(es)", matches);
  rex::FlushLogging();
}

// mem_find_floats: guest addresses where these floats follow each other, each
// within `tolerance` (the last argument when it starts with ~, default 0.01).
void MemFindFloats(std::string_view args) {
  std::vector<float> values;
  float tolerance = 0.01f;
  for (const std::string& part : SplitArgs(args)) {
    if (part[0] == '~') {
      tolerance = std::strtof(part.c_str() + 1, nullptr);
    } else {
      values.push_back(std::strtof(part.c_str(), nullptr));
    }
  }
  if (values.empty()) {
    REXLOG_WARN("mem_find_floats: usage: mem_find_floats <value> [<value> ...] [~tolerance]");
    return;
  }
  size_t matches = 0;
  ForEachCommittedRange([&](uint32_t address, const uint8_t* host, size_t size) {
    const size_t needed = values.size() * 4;
    for (size_t offset = 0; offset + needed <= size && matches < kMaxMatches; offset += 4) {
      bool match = true;
      for (size_t i = 0; i < values.size(); ++i) {
        const float value = rex::memory::load_and_swap<float>(host + offset + i * 4);
        if (!(std::fabs(value - values[i]) <= tolerance)) {
          match = false;
          break;
        }
      }
      if (match) {
        REXLOG_WARN("mem_find_floats: {:08X}", address + uint32_t(offset));
        ++matches;
      }
    }
  });
  REXLOG_WARN("mem_find_floats: {} match(es)", matches);
  rex::FlushLogging();
}

// mem_dump: <hex address> [count]: 32-bit words as hex and float.
void MemDump(std::string_view args) {
  const std::vector<std::string> parts = SplitArgs(args);
  if (parts.empty()) {
    REXLOG_WARN("mem_dump: usage: mem_dump <hex address> [count]");
    return;
  }
  const uint32_t address = uint32_t(std::strtoul(parts[0].c_str(), nullptr, 16)) & ~3u;
  const uint32_t count =
      std::min<uint32_t>(parts.size() > 1 ? uint32_t(std::strtoul(parts[1].c_str(), nullptr, 0))
                                          : 16u,
                         512u);
  auto* runtime = rex::Runtime::instance();
  auto* memory = runtime ? runtime->memory() : nullptr;
  if (!memory) {
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t word_address = address + i * 4;
    auto* heap = memory->LookupHeap(word_address);
    rex::memory::HeapAllocationInfo info = {};
    if (!heap || !heap->QueryRegionInfo(word_address, &info) ||
        !(info.state & rex::memory::kMemoryAllocationCommit)) {
      REXLOG_WARN("mem_dump: {:08X}: not committed", word_address);
      break;
    }
    const uint8_t* host = memory->TranslateVirtual<const uint8_t*>(word_address);
    const uint32_t word = rex::memory::load_and_swap<uint32_t>(host);
    float value;
    std::memcpy(&value, &word, sizeof(value));
    REXLOG_WARN("mem_dump: {:08X} (+{:3}): {:08X} {}", word_address, i * 4, word, value);
  }
  rex::FlushLogging();
}

}  // namespace

REXCVAR_DEFINE_COMMAND_ARGS(mem_find_words, MemFindWords, "Debug",
                            "Find big-endian 32-bit words (hex) in guest memory");
REXCVAR_DEFINE_COMMAND_ARGS(mem_find_floats, MemFindFloats, "Debug",
                            "Find floats in guest memory (last argument ~tolerance)");
REXCVAR_DEFINE_COMMAND_ARGS(mem_dump, MemDump, "Debug",
                            "Dump guest memory words: <hex address> [count]");
