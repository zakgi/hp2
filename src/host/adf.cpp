#include "host/adf.hpp"

#include <adf_dir.h>
#include <adf_file.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace hp2::host {

namespace {

std::string Upper(std::string_view text) {
  auto upper = std::string{text};
  std::ranges::transform(upper, upper.begin(), [](char letter) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
  });
  return upper;
}

}  // namespace

AdfImageManager::AdfImageManager() {
  adfLibInit();
}

AdfImageManager::~AdfImageManager() {
  images_.clear();
  adfLibCleanUp();
}

bool AdfImageManager::AddDiskImage(const std::filesystem::path& path) {
  auto result = false;
  if (not std::filesystem::is_regular_file(path)) {
    spdlog::warn("File {} does not exist", path.string());
  } else if (images_.contains(path)) {
    spdlog::warn("Image {} is already mounted", path.string());
  } else {
    auto* device = adfDevOpen(path.c_str(), ADF_ACCESS_MODE_READONLY);
    if (device != nullptr and adfDevMount(device) == ADF_RC_OK) {
      auto* volume = adfVolMount(device, 0, ADF_ACCESS_MODE_READONLY);
      auto image = AdfImage{device, volume};
      spdlog::info("Mounting disk image {}", path.string());
      image.PopulateDirectory();
      images_.try_emplace(path, std::move(image));
      result = true;
    } else if (device != nullptr) {
      adfDevClose(device);
    }
  }
  return result;
}

std::optional<std::span<const std::uint8_t>> AdfImageManager::GetFile(std::string_view name) const {
  auto result = std::optional<std::span<const std::uint8_t>>{};
  for (const auto& [path, image] : images_) {
    if (not result) {
      result = image.GetFile(name);
    }
  }
  return result;
}

std::optional<std::span<const std::uint8_t>> AdfImage::GetFile(std::string_view name) const {
  auto result = std::optional<std::span<const std::uint8_t>>{};
  if (const auto item = file_list_.find(Upper(name)); item != file_list_.end()) {
    result = item->second.data;
  }
  return result;
}

void AdfImage::PopulateDirectory() {
  if (volume_ != nullptr) {
    adfToRootDir(volume_);
    auto* root = adfGetRDirEnt(volume_, volume_->curDirPtr, true);
    TraverseDirectory(root, std::filesystem::path{});
    adfFreeDirList(root);
  }
}

void AdfImage::TraverseDirectory(AdfList* node, const std::filesystem::path& path) {
  while (node != nullptr) {
    const auto* entry = static_cast<AdfEntry*>(node->content);
    if (entry->type == ADF_ST_FILE) {
      const auto full_path = (path / entry->name).generic_string();
      spdlog::debug("Found file {}, size {}", full_path, entry->size);
      file_list_.try_emplace(Upper(full_path), path, entry->name, entry->size,
                             ReadDataFromFile(path, entry->name, entry->size));
    }
    if (node->subdir != nullptr) {
      TraverseDirectory(node->subdir, path / entry->name);
    }
    node = node->next;
  }
}

std::vector<std::uint8_t> AdfImage::ReadDataFromFile(const std::filesystem::path& path, const std::string& name,
                                                     std::size_t size) {
  auto data = std::vector<std::uint8_t>{};
  spdlog::debug("Opening file \"{}\"", name);
  adfToRootDir(volume_);
  auto change_dir_ok = true;
  for (const auto& subdir : path) {
    if (change_dir_ok and adfChangeDir(volume_, subdir.c_str()) != ADF_RC_OK) {
      spdlog::error("Change dir: {} failed", subdir.string());
      change_dir_ok = false;
    }
  }
  if (change_dir_ok) {
    auto* file = adfFileOpen(volume_, name.c_str(), ADF_FILE_MODE_READ);
    data.resize(size);
    if (file == nullptr) {
      spdlog::error("Error while opening file \"{}\"", name);
    } else {
      const auto read_bytes = adfFileRead(file, static_cast<std::uint32_t>(size), data.data());
      spdlog::debug("Read {} bytes, expected {}", read_bytes, size);
      adfFileClose(file);
    }
  }
  return data;
}

AdfImage::~AdfImage() {
  if (volume_ != nullptr) {
    adfVolUnMount(volume_);
  }
  if (device_ != nullptr) {
    if (device_->mounted) {
      adfDevUnMount(device_);
    }
    adfDevClose(device_);
  }
}

}  // namespace hp2::host
