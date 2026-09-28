#pragma once

#include <filesystem>
#include <windows.h>

#include <rex/rex_app.h>

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
};

