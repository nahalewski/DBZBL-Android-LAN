#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include <rex/rex_app.h>
#include <rex/ui/overlay/quick_menu.h>

// burstlimit_patches.cpp
void BurstLimitApplyPostEffectSettings();
// burstlimit_forms.cpp
std::unique_ptr<rex::ui::ImGuiDialog> BurstLimitCreateStartFormTags(
    rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
    const std::filesystem::path& game_data_root);

class BurstlimitApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BurstlimitApp>(
        new BurstlimitApp(ctx, "burstlimit", PPCImageConfig));
  }

  // Burst Limit only ships with the Xenos (D3D12) renderer, so use it when no
  // gpu_plugin was configured instead of starting headless.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    BurstLimitApplyPostEffectSettings();
  }

  // The start form tags of the character select (burstlimit_forms.cpp).
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    start_form_tags_ = BurstLimitCreateStartFormTags(drawer, immediate_drawer(), game_data_root());
  }

  void OnShutdown() override { start_form_tags_.reset(); }

  // The settings menu (F1 or Back + Start on the controller).
  void OnConfigureQuickMenu(rex::ui::QuickMenuConfig& menu) override {
    using Item = rex::ui::QuickMenuItem;
    using Choices = std::vector<std::pair<std::string, std::string>>;
    auto toggle = [](std::string label, std::string cvar, std::string help) {
      Item item;
      item.kind = Item::Kind::kToggle;
      item.label = std::move(label);
      item.cvar = std::move(cvar);
      item.help = std::move(help);
      return item;
    };
    auto choice = [](std::string label, std::string cvar, Choices choices, std::string help) {
      Item item;
      item.kind = Item::Kind::kChoice;
      item.label = std::move(label);
      item.cvar = std::move(cvar);
      item.choices = std::move(choices);
      item.help = std::move(help);
      return item;
    };
    auto number = [](std::string label, std::string cvar, double min, double max, double step,
                     std::string help) {
      Item item;
      item.kind = Item::Kind::kNumber;
      item.label = std::move(label);
      item.cvar = std::move(cvar);
      item.min = min;
      item.max = max;
      item.step = step;
      item.help = std::move(help);
      return item;
    };
    const std::vector<std::string> fsr_effects = {"fsr", "fsr2", "fsr3"};

    menu.title = "RECOMP SETTINGS";

    rex::ui::QuickMenuSection& graphics = menu.sections.emplace_back();
    graphics.title = "GRAPHICS";
    Item& resolution = graphics.items.emplace_back(choice(
        "Resolution", "draw_resolution_scale_x",
        {{"1", "1280x720"}, {"2", "2560x1440"}, {"3", "3840x2160 (4K)"}, {"4", "5120x2880"}},
        "The resolution the game renders at. Higher is sharper but needs a faster GPU."));
    resolution.mirrored_cvars = {"draw_resolution_scale_y", "resolution_scale"};
    graphics.items.push_back(choice(
        "Upscaler", "present_effect",
        {{"bilinear", "Off"},
         {"cas", "AMD CAS"},
         {"fsr", "AMD FSR 1"},
         {"fsr2", "AMD FSR 2"},
         {"fsr3", "AMD FSR 3"}},
        "FSR renders below the resolution above and upscales to it, for more FPS. CAS keeps "
        "the resolution and only sharpens."));
    Item& quality = graphics.items.emplace_back(choice(
        "Upscaler quality", "present_fsr_quality_mode",
        {{"auto", "Native"},
         {"nativeaa", "Native AA"},
         {"quality", "Quality"},
         {"balanced", "Balanced"},
         {"performance", "Performance"},
         {"ultra_performance", "Ultra Performance"}},
        "How far below the resolution FSR renders. Lower settings give more FPS and a softer "
        "image."));
    quality.shown_if_cvar = "present_effect";
    quality.shown_if_values = fsr_effects;
    Item& cas_sharpness = graphics.items.emplace_back(
        number("Sharpness", "present_cas_additional_sharpness", 0.0, 1.0, 0.1,
               "Extra sharpening on top of AMD CAS."));
    cas_sharpness.display_scale = 100.0;
    cas_sharpness.format = "%.0f%%";
    cas_sharpness.shown_if_cvar = "present_effect";
    cas_sharpness.shown_if_values = {"cas"};
    // FSR takes a sharpness reduction in stops: 0 = sharpest.
    Item& fsr_sharpness = graphics.items.emplace_back(
        number("Sharpness", "present_fsr_sharpness_reduction", 0.0, 2.0, 0.2,
               "Sharpening after the FSR upscale."));
    fsr_sharpness.display_scale = -50.0;
    fsr_sharpness.display_offset = 100.0;
    fsr_sharpness.format = "%.0f%%";
    fsr_sharpness.shown_if_cvar = "present_effect";
    fsr_sharpness.shown_if_values = fsr_effects;
    graphics.items.push_back(choice(
        "Anti-aliasing", "swap_post_effect",
        {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA (strong)"}},
        "Smooths jagged edges. Softens the image a little."));
    graphics.items.push_back(choice(
        "NVIDIA DLAA", "dlss_mode", {{"off", "Off"}, {"dlaa", "On"}},
        "NVIDIA's AI anti-aliasing for the 3D scene, at the resolution above (RTX GPUs). The "
        "HUD stays as it is."));
    Item& dlss_preset = graphics.items.emplace_back(choice(
        "DLSS model", "dlss_preset", {{"k", "K"}, {"l", "L"}, {"m", "M"}},
        "NVIDIA's DLSS models. M is sharper and more stable than K; L is like M but slower."));
    dlss_preset.shown_if_cvar = "dlss_mode";
    dlss_preset.shown_if_values = {"dlaa"};
    graphics.items.push_back(choice(
        "Texture filtering", "anisotropic_override",
        {{"-1", "Game"}, {"1", "1x"}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"}},
        "Keeps textures sharp when seen at an angle, like the floor."));
    graphics.items.push_back(toggle(
        "Texture pack", "texture_replace_enabled",
        "HD textures from the textures\\replace folder next to burstlimit.exe."));
    Item& preload = graphics.items.emplace_back(toggle(
        "Preload textures", "texture_replace_preload",
        "Loads the whole texture pack into memory at startup, so textures don't stutter the "
        "first time they show up. Applies after a restart."));
    preload.shown_if_cvar = "texture_replace_enabled";
    preload.shown_if_values = {"true"};

    rex::ui::QuickMenuSection& effects = menu.sections.emplace_back();
    effects.title = "EFFECTS";
    Item& fov = effects.items.emplace_back(
        number("Field of view", "field_of_view", 50.0, 200.0, 5.0,
               "How much of the stage you see. 100% is the original view, higher is wider."));
    fov.format = "%.0f%%";
    effects.items.push_back(toggle("Depth of field", "depth_of_field",
                                   "Blurs the background behind the fighters."));
    effects.items.push_back(toggle(
        "Glow blur", "glow_blur",
        "Soft glow around bright parts. Leaves a halo around the characters at high "
        "resolution."));
    effects.items.push_back(
        toggle("Motion blur", "motion_blur", "Directional blur during fast moves."));

    rex::ui::QuickMenuSection& game = menu.sections.emplace_back();
    game.title = "GAME";
    game.items.push_back(choice(
        "Frame rate", "frame_rate",
        {{"30", "30 FPS"},
         {"60", "60 FPS"},
         {"120", "120 FPS"},
         {"144", "144 FPS"},
         {"unlocked", "Unlocked"}},
        "The most frames per second the game runs at. 30 is the original. Above 60 needs a "
        "monitor with a high refresh rate to see the difference."));
    Item& online = game.items.emplace_back(choice(
        "Online input speed", "online_tick_sleep",
        {{"0", "LAN"}, {"1", "Fast"}, {"2", "Normal"}, {"3", "Game default"}},
        "How often online matches exchange inputs: Fast for a good ping, Normal for an "
        "average one. Both players must pick the same."));
    online.shown_if_cvar = "online_fast_tick";
    online.shown_if_values = {"true"};
    game.items.push_back(toggle("Vibration", "vibration", "Controller vibration."));
    game.items.push_back(toggle(
        "Free camera", "free_camera",
        "Fly the camera (Y in this menu; pause first to freeze the action): left stick "
        "moves, right stick looks, LB/RB down/up, LT/RT slower/faster, D-pad zoom and tilt, A "
        "HUD, Y resets, B exits."));
    menu.quick_toggle_label = "Free camera";
    menu.quick_toggle_cvar = "free_camera";

    rex::ui::QuickMenuSection& display = menu.sections.emplace_back();
    display.title = "DISPLAY";
    display.items.push_back(toggle("Fullscreen", "fullscreen", "Borderless fullscreen."));
    display.items.push_back(toggle(
        "Show FPS", "debug_overlay",
        "Frame rate, frame time, render resolution and upscaler in a corner (F3)."));
    Item& fps_position = display.items.emplace_back(choice(
        "FPS position", "debug_overlay_position",
        {{"top-left", "Top left"},
         {"top-right", "Top right"},
         {"bottom-left", "Bottom left"},
         {"bottom-right", "Bottom right"}},
        "Which corner the frame rate panel sits in."));
    fps_position.shown_if_cvar = "debug_overlay";
    fps_position.shown_if_values = {"true"};
    display.items.push_back(choice(
        "Menu buttons", "quick_menu_buttons",
        {{"back+start", "Back + Start"}, {"l3+r3", "L3 + R3"}, {"none", "Keyboard only"}},
        "Controller buttons that open this menu. F1 on the keyboard always does."));
  }

  // Portable build: always load game files beside burstlimit.exe.
  // This intentionally ignores stale game_data_root values from old configs.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    wchar_t exe_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

    if (length == 0 || length >= MAX_PATH) {
      return;
    }

    const auto local_game_root =
        std::filesystem::path(exe_path).parent_path() / L"game_data_root";

    if (std::filesystem::exists(local_game_root / L"default.xex")) {
      paths.game_data_root = local_game_root;
    }
  }

 private:
  std::unique_ptr<rex::ui::ImGuiDialog> start_form_tags_;
};
