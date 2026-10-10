#include "burstlimit_game_import.h"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "verify")
    return burstlimit::install::VerifyData(argv[2]) ? 0 : 1;
  if (argc < 3) return 2;
  std::atomic<bool> cancel{false};
  std::string error;
  std::thread worker([&] {
    try {
      burstlimit::install::Import(argv[1], argv[2], cancel,
          [&](uint64_t copied, uint64_t total, const std::string& file) {
            if (argc == 4 && copied >= std::stoull(argv[3])) cancel = true;
          });
    } catch (const std::exception& e) { error = e.what(); }
  });
  worker.join();
  if (error.empty()) { std::cout << "INSTALLED\n"; return 0; }
  std::cerr << error << '\n';
  return 1;
}
