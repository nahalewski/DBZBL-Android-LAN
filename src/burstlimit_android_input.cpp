#include "burstlimit_android_input.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <jni.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <arpa/inet.h>
#include <cstdlib>
#include <rex/net/online.h>

namespace burstlimit::android {
namespace {
std::mutex mutex;
SDL_JoystickID joystick_id = 0;
SDL_Joystick* joystick = nullptr;

Sint16 Stick(float value) {
  if (!std::isfinite(value)) value = 0;
  value = std::clamp(value, -1.0f, 1.0f);
  return static_cast<Sint16>(value * (value < 0 ? 32768.0f : 32767.0f));
}
Sint16 Trigger(float value) {
  if (!std::isfinite(value)) value = 0;
  return static_cast<Sint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f - 32768.0f);
}
void State(uint32_t buttons, float lx, float ly, float rx, float ry,
           float lt, float rt) {
  if (!joystick) return;
  static uint32_t last_buttons = 0;
  if (buttons != last_buttons) {
    __android_log_print(ANDROID_LOG_DEBUG, "BurstLimitControls", "Touch buttons: %08X", buttons);
    last_buttons = buttons;
  }
  SDL_LockJoysticks();
  for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
    SDL_SetJoystickVirtualButton(joystick, i, (buttons & (uint32_t{1} << i)) != 0);
  const Sint16 axes[] = {Stick(lx), Stick(ly), Stick(rx), Stick(ry), Trigger(lt), Trigger(rt)};
  for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
    SDL_SetJoystickVirtualAxis(joystick, i, axes[i]);
  SDL_UnlockJoysticks();
}
}  // namespace

bool EnableTouchController(bool enabled) {
  std::lock_guard lock(mutex);
  if (!enabled) {
    if (joystick) {
      State(0, 0, 0, 0, 0, 0, 0);
      SDL_CloseJoystick(joystick);
      joystick = nullptr;
      SDL_DetachVirtualJoystick(joystick_id);
      joystick_id = 0;
      __android_log_print(ANDROID_LOG_INFO, "BurstLimitControls", "Touch controller detached");
    }
    return true;
  }
  if (joystick) return true;
  // Activity.onResume can precede the native runtime's input initialization.
  if (!(SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD)) return false;
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.vendor_id = 0x045e;
  desc.product_id = 0x028e;
  desc.name = "Burst Limit touch controller";
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.axis_mask = (uint32_t{1} << SDL_GAMEPAD_AXIS_COUNT) - 1;
  desc.button_mask = (uint32_t{1} << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  joystick_id = SDL_AttachVirtualJoystick(&desc);
  if (!joystick_id) return false;
  joystick = SDL_OpenJoystick(joystick_id);
  if (!joystick) {
    SDL_DetachVirtualJoystick(joystick_id);
    joystick_id = 0;
    return false;
  }
  State(0, 0, 0, 0, 0, 0, 0);
  __android_log_print(ANDROID_LOG_INFO, "BurstLimitControls", "Touch controller attached: %u", joystick_id);
  return true;
}

void SetTouchControllerState(uint32_t buttons, float lx, float ly, float rx,
                             float ry, float lt, float rt) {
  std::lock_guard lock(mutex);
  State(buttons, lx, ly, rx, ry, lt, rt);
}
}  // namespace burstlimit::android

extern "C" JNIEXPORT jboolean JNICALL
Java_org_burstlimit_android_BurstLimitActivity_nativeSetTouchControlsEnabled(
    JNIEnv*, jclass, jboolean enabled) {
  return burstlimit::android::EnableTouchController(enabled);
}
extern "C" JNIEXPORT void JNICALL
Java_org_burstlimit_android_BurstLimitActivity_nativeSetTouchState(
    JNIEnv*, jclass, jint buttons, jfloat lx, jfloat ly, jfloat rx, jfloat ry,
    jfloat lt, jfloat rt) {
  burstlimit::android::SetTouchControllerState(buttons, lx, ly, rx, ry, lt, rt);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_burstlimit_android_BurstLimitActivity_nativeSetGamertag(
    JNIEnv* env, jclass, jstring name) {
  const char* text = env->GetStringUTFChars(name, nullptr);
  if (!text) return false;
  const bool success = rex::net::online::SetPlayerName(text);
  env->ReleaseStringUTFChars(name, text);
  return success;
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_burstlimit_android_BurstLimitActivity_nativeGetGamertag(JNIEnv* env, jclass) {
  return env->NewStringUTF(rex::net::online::PlayerName().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_burstlimit_android_BurstLimitActivity_nativeConfigureLan(
    JNIEnv* env, jclass, jstring local, jstring host) {
  const char* local_ip = env->GetStringUTFChars(local, nullptr);
  const char* host_ip = env->GetStringUTFChars(host, nullptr);
  if (!local_ip || !host_ip) {
    if (local_ip) env->ReleaseStringUTFChars(local, local_ip);
    if (host_ip) env->ReleaseStringUTFChars(host, host_ip);
    return false;
  }
  in_addr parsed{};
  const auto valid = [&parsed](const char* ip) {
    return inet_pton(AF_INET, ip, &parsed) == 1 && ntohl(parsed.s_addr) > 0 &&
           (ntohl(parsed.s_addr) >> 24) < 224;
  };
  const bool ok = (!*local_ip && !*host_ip) ||
                  (valid(local_ip) && (!*host_ip || valid(host_ip)));
  // Configure before the native main thread initializes the online layer.
  unsetenv("REX_XNET_IP");
  unsetenv("REX_XNET_BIND_IP");
  unsetenv("REX_XNET_SEARCH_IP");
  setenv("REX_LAN_DISABLED", ok && *local_ip ? "0" : "1", 1);
  if (ok && *local_ip) {
    setenv("REX_XNET_IP", local_ip, 1);
    // Keep the guest's wildcard UDP bind. Burst Limit rewrites its own
    // match-start destination to 127.0.0.1; a Wi-Fi-only bind drops that
    // required self notification even while remote lobby traffic works.
    if (*host_ip) setenv("REX_XNET_SEARCH_IP", host_ip, 1);
  }
  __android_log_print(ANDROID_LOG_INFO, "BurstLimitLAN", "%s local=%s host=%s",
                      ok ? "Configured" : "Invalid addresses", local_ip, host_ip);
  env->ReleaseStringUTFChars(local, local_ip);
  env->ReleaseStringUTFChars(host, host_ip);
  return ok;
}
