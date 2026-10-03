#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "host/asset_manager.hpp"

namespace hp2::test {

// The extracted game disk the asset-dependent tests read (HP2_ASSET_DIR, set by CMake).
inline std::filesystem::path GameDir() {
  return std::filesystem::path{HP2_ASSET_DIR};
}

// A data file of DISK2_2/, matched without regard to case; empty when absent.
inline std::optional<std::vector<std::uint8_t>> DataFile(std::string_view name) {
  const auto directory = host::FindFile(GameDir(), "DISK2_2");
  const auto path = directory ? host::FindFile(*directory, name) : std::nullopt;
  return path ? host::ReadFile(*path) : std::nullopt;
}

// hp.prg; empty when absent.
inline std::optional<std::vector<std::uint8_t>> Executable() {
  const auto path = host::FindFile(GameDir(), "hp.prg");
  return path ? host::ReadFile(*path) : std::nullopt;
}

}  // namespace hp2::test
