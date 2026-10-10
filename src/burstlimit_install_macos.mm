#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "burstlimit_game_import.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <string>
#include <thread>

REXCVAR_DEFINE_STRING(install_iso, "", "Setup",
                      "First-run setup: import this USA ISO or game ZIP instead of opening the file picker");

namespace {
struct SetupState {
  std::atomic<bool> cancel{false}, finished{false};
  std::atomic<uint64_t> copied{0}, total{0};
  std::mutex mutex;
  std::string file, error;
};
NSString* Text(const std::string& value) {
  return [[NSString alloc] initWithBytes:value.data() length:value.size()
                               encoding:NSUTF8StringEncoding] ?: @"Couldn't read the game file.";
}
}  // namespace

@interface BLSetupCancel : NSObject {
 @public
  std::shared_ptr<SetupState> state;
}
- (void)cancel:(id)sender;
@end
@implementation BLSetupCancel
- (void)cancel:(id)sender { state->cancel = true; }
@end

namespace {
std::filesystem::path ChooseImage() {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.title = @"Welcome to Burst Limit";
  panel.message = @"Select your Dragon Ball Z: Burst Limit (USA) Xbox 360 ISO, or a ZIP containing the extracted USA game files. Setup needs about 3.4 GB and only happens once.";
  panel.prompt = @"Install game";
  panel.canChooseDirectories = NO;
  panel.canChooseFiles = YES;
  panel.allowsMultipleSelection = NO;
  panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"iso"] ?: UTTypeData, UTTypeZIP];
  if ([panel runModal] == NSModalResponseOK && panel.URL.fileSystemRepresentation)
    return std::filesystem::path(panel.URL.fileSystemRepresentation);
  return {};
}

std::string InstallWithProgress(const std::filesystem::path& source,
                                const std::filesystem::path& target) {
  auto state = std::make_shared<SetupState>();
  NSPanel* window = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 620, 190)
      styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
  window.title = @"Installing Burst Limit";
  window.releasedWhenClosed = NO;
  NSTextField* heading = [NSTextField labelWithString:@"Checking your USA game files..."];
  heading.frame = NSMakeRect(24, 134, 572, 24);
  heading.font = [NSFont boldSystemFontOfSize:15];
  [window.contentView addSubview:heading];
  NSProgressIndicator* progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(24, 105, 572, 16)];
  progress.style = NSProgressIndicatorStyleBar;
  progress.indeterminate = YES;
  progress.minValue = 0;
  progress.maxValue = 1;
  [progress startAnimation:nil];
  [window.contentView addSubview:progress];
  NSTextField* detail = [NSTextField labelWithString:@"Your original ISO or ZIP stays unchanged."];
  detail.frame = NSMakeRect(24, 65, 572, 28);
  detail.lineBreakMode = NSLineBreakByTruncatingMiddle;
  [window.contentView addSubview:detail];
  // NSControl's target is not retained. Keep it alive through the modal loop.
  __attribute__((objc_precise_lifetime)) BLSetupCancel* cancelTarget = [BLSetupCancel new];
  cancelTarget->state = state;
  NSButton* cancel = [NSButton buttonWithTitle:@"Cancel setup" target:cancelTarget action:@selector(cancel:)];
  cancel.frame = NSMakeRect(464, 20, 132, 32);
  [window.contentView addSubview:cancel];
  [window center];
  [window makeKeyAndOrderFront:nil];

  std::thread worker([state, source, target] {
    try {
      REXLOG_INFO("Mac setup: importing {}", source.string());
      burstlimit::install::Import(source, target, state->cancel,
          [state](uint64_t copied, uint64_t total, const std::string& file) {
            state->total = total;
            state->copied = copied;
            std::lock_guard lock(state->mutex);
            state->file = file;
          });
      REXLOG_INFO("Mac setup: verified game installed in {}", target.string());
    } catch (const std::exception& e) {
      std::lock_guard lock(state->mutex);
      state->error = e.what();
    }
    state->finished = true;
  });

  // Cocoa pumps input and redraws while the worker extracts files. No Vulkan
  // renderer, game window or guest threads exist during this first-run step.
  NSTimer* timer = [NSTimer timerWithTimeInterval:.1 repeats:YES block:^(NSTimer*) {
    const uint64_t total = state->total.load(), copied = state->copied.load();
    if (total) {
      progress.indeterminate = NO;
      progress.doubleValue = double(copied) / double(total);
      heading.stringValue = [NSString stringWithFormat:@"Installing game files — %.0f%%",
          100.0 * double(copied) / double(total)];
      std::lock_guard lock(state->mutex);
      detail.stringValue = Text(state->file);
    }
    if (state->cancel) {
      heading.stringValue = @"Canceling setup...";
      cancel.enabled = NO;
    }
    if (state->finished) [NSApp stopModal];
  }];
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
  [NSApp runModalForWindow:window];
  [timer invalidate];
  // A system quit or unexpected modal close must also stop the worker before
  // its state/UI can be destroyed. The final installed folder is atomic.
  if (!state->finished) state->cancel = true;
  worker.join();
  [window orderOut:nil];
  [window close];
  std::lock_guard lock(state->mutex);
  return state->error;
}
}  // namespace

bool BurstLimitVerifyMacData(const std::filesystem::path& root) {
  return burstlimit::install::VerifyData(root);
}

bool BurstLimitRunMacSetup(const std::filesystem::path& target) {
  @autoreleasepool {
    std::filesystem::path source(REXCVAR_GET(install_iso));
    for (;;) {
      if (source.empty()) source = ChooseImage();
      if (source.empty()) return false;
      const std::string error = InstallWithProgress(source, target);
      if (error.empty()) return true;
      if (error.starts_with("Setup canceled")) return false;
      NSAlert* alert = [NSAlert new];
      alert.messageText = @"Couldn't install Burst Limit";
      alert.informativeText = Text(error);
      [alert addButtonWithTitle:@"Choose another file"];
      [alert addButtonWithTitle:@"Quit"];
      if ([alert runModal] != NSAlertFirstButtonReturn) return false;
      source.clear();
    }
  }
}
