#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace burstlimit::install {
using Progress = std::function<void(uint64_t, uint64_t, const std::string&)>;

bool VerifyData(const std::filesystem::path& root);

// Reads the player's ISO or ZIP without modifying it. Verifies the USA
// executable before extraction, then atomically publishes a complete install.
// Throws std::runtime_error on invalid input, cancellation or an I/O failure.
void Import(const std::filesystem::path& source,
            const std::filesystem::path& target,
            const std::atomic<bool>& cancel, const Progress& progress);
}  // namespace burstlimit::install
