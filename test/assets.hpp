#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "host/adf.hpp"

namespace hp2::test {

// The game disk image the asset-dependent tests read (HP2_DISK_IMAGE, set by CMake).
inline std::filesystem::path DiskImage() {
  return std::filesystem::path{HP2_DISK_IMAGE};
}

// The file at `path` on the game disk; empty when the image or the file is absent.
inline std::optional<std::vector<std::uint8_t>> DiskFile(std::string_view path) {
  auto result = std::optional<std::vector<std::uint8_t>>{};
  auto disk = host::AdfImageManager{};
  if (disk.AddDiskImage(DiskImage())) {
    if (const auto bytes = disk.GetFile(path)) {
      result = std::vector<std::uint8_t>(bytes->begin(), bytes->end());
    }
  }
  return result;
}

// A data file of DISK2_2/; empty when absent.
inline std::optional<std::vector<std::uint8_t>> DataFile(std::string_view name) {
  return DiskFile(std::string{"DISK2_2/"} + std::string{name});
}

// hp.prg; empty when absent.
inline std::optional<std::vector<std::uint8_t>> Executable() {
  return DiskFile("hp.prg");
}

}  // namespace hp2::test
