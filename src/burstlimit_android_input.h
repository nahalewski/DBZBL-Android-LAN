#pragma once

#include <cstdint>

// The overlay supplies SDL button indices and normalized SDL axes. The SDK
// converts this controller through the same XInput path as a hardware pad.
namespace burstlimit::android {
bool EnableTouchController(bool enabled);
void SetTouchControllerState(uint32_t buttons, float lx, float ly, float rx,
                             float ry, float lt, float rt);
}  // namespace burstlimit::android
