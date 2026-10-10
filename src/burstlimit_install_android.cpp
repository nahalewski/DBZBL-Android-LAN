// Android receives the same verified data extracted for host code generation.
// It stores the game outside the APK and does not require broad storage access.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <memory>
#include <rex/ui/imgui_dialog.h>
#include <string>
#include <vector>
extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace {
bool VerifyData(const std::filesystem::path &root) {
  std::error_code error;
  if (std::filesystem::file_size(root / "default.xex", error) != 7983104 ||
      error)
    return false;
  std::ifstream input(root / "default.xex", std::ios::binary);
  if (!input)
    return false;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
  if (data.size() != 7983104)
    return false;
  auto *sha = av_sha_alloc();
  if (!sha)
    return false;
  uint8_t digest[20];
  av_sha_init(sha, 160);
  av_sha_update(sha, data.data(), data.size());
  av_sha_final(sha, digest);
  av_free(sha);
  static const uint8_t expected[] = {0xae, 0xc5, 0x98, 0xf8, 0x8c, 0xf5, 0x11,
                                     0x81, 0xfc, 0x37, 0x7b, 0x14, 0x8e, 0x0b,
                                     0x1a, 0xd3, 0x0d, 0xb4, 0x48, 0x5c};
  if (!std::equal(std::begin(expected), std::end(expected), digest))
    return false;
  for (const char *name :
       {"LONG2DATA_US.CPK", "STREAM_US.CPK", "STREAM_JP.CPK"}) {
    if (!std::filesystem::is_regular_file(root / "LONG2DATA" / name))
      return false;
  }
  return true;
}

class AndroidDataDialog final : public rex::ui::ImGuiDialog {
public:
  AndroidDataDialog(rex::ui::ImGuiDrawer *drawer, std::filesystem::path target,
                    std::function<void(std::filesystem::path)> done,
                    std::function<void(std::function<void()>)> defer)
      : ImGuiDialog(drawer), target_(std::move(target)), done_(std::move(done)),
        defer_(std::move(defer)) {}
  void OnDraw(ImGuiIO &io) override {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Game data required", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextWrapped(
        "Game files are missing or changed. Close and reopen Burst Limit "
        "to choose your USA Xbox 360 ISO or extracted-game ZIP.");
    ImGui::TextWrapped("%s", target_.string().c_str());
    ImGui::TextWrapped("The Android setup screen verifies and installs your "
                       "game in app storage. Game data is not included in this APK.");
    if (ImGui::Button("Check game files", ImVec2(250, 60))) {
      if (VerifyData(target_)) {
        auto root = target_;
        auto done = done_;
        defer_([root, done] { done(root); });
      } else
        failed_ = true;
    }
    if (failed_)
      ImGui::TextWrapped("Missing files or unsupported default.xex. Use the "
                         "verified USA data folder.");
    ImGui::End();
  }

private:
  std::filesystem::path target_;
  std::function<void(std::filesystem::path)> done_;
  std::function<void(std::function<void()>)> defer_;
  bool failed_ = false;
};
} // namespace

bool BurstLimitVerifyAndroidData(const std::filesystem::path &root) {
  return !root.empty() && VerifyData(root);
}

std::unique_ptr<rex::ui::ImGuiDialog>
BurstLimitCreateInstaller(rex::ui::ImGuiDrawer *drawer,
                          const std::filesystem::path &target,
                          std::function<void(std::filesystem::path)> done,
                          std::function<void(std::function<void()>)> defer) {
  return std::make_unique<AndroidDataDialog>(drawer, target, std::move(done),
                                             std::move(defer));
}
