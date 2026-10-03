#pragma once

#include <adflib.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hp2::host {

struct FileEntry {
  std::filesystem::path path;
  std::string name;
  std::size_t size;
  std::vector<std::uint8_t> data;
};

// The files of one mounted disk image, read when the image is added. Names match without regard
// to case, as AmigaDOS names do: the disk has DISK2_2/Sirene.snd where the program asks for
// DISK2_2/SIRENE.SND.
class AdfImage final {
 public:
  AdfImage(AdfDevice* device, AdfVolume* volume) : device_(device), volume_(volume) {}
  AdfImage(AdfImage&& image) noexcept
      : device_(image.device_), volume_(image.volume_), file_list_(std::move(image.file_list_)) {
    image.device_ = nullptr;
    image.volume_ = nullptr;
    image.file_list_ = decltype(file_list_){};
  }
  AdfImage(const AdfImage&) = delete;
  AdfImage& operator=(const AdfImage&) = delete;
  AdfImage& operator=(AdfImage&&) = delete;
  ~AdfImage();

  void PopulateDirectory();

  [[nodiscard]] std::optional<std::span<const std::uint8_t>> GetFile(std::string_view name) const;

 private:
  std::vector<std::uint8_t> ReadDataFromFile(const std::filesystem::path& path, const std::string& name,
                                             std::size_t size);
  void TraverseDirectory(AdfList* node, const std::filesystem::path& path);

  AdfDevice* device_;
  AdfVolume* volume_;
  // By path, upper case.
  std::map<std::string, FileEntry> file_list_;
};

// The mounted disk images; ADFlib is initialised for as long as the manager lives, so one manager
// exists at a time.
class AdfImageManager final {
 public:
  AdfImageManager();
  ~AdfImageManager();
  AdfImageManager(const AdfImageManager&) = delete;
  AdfImageManager& operator=(const AdfImageManager&) = delete;
  AdfImageManager(AdfImageManager&&) = delete;
  AdfImageManager& operator=(AdfImageManager&&) = delete;

  bool AddDiskImage(const std::filesystem::path& path);
  // The file `name` (a path from the disk's root) of the first image that has it.
  [[nodiscard]] std::optional<std::span<const std::uint8_t>> GetFile(std::string_view name) const;

 private:
  std::map<std::filesystem::path, AdfImage> images_;
};

}  // namespace hp2::host
