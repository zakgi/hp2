#include "host/format/object_placement.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#include "host/format/endian.hpp"

namespace hp2::host {

namespace {

constexpr auto kNoObjects = std::uint16_t{0xffff};
constexpr auto kCountMask = std::uint16_t{0xff};
constexpr auto kEntryBytes = std::size_t{10};

}  // namespace

std::expected<ObjectPlacement, ObjectPlacementError> DecodeObjectPlacement(std::span<const std::uint8_t> file) {
  const auto bytes = std::as_bytes(file);
  auto result = std::expected<ObjectPlacement, ObjectPlacementError>{ObjectPlacement{}};
  if (file.size() < 4 * kRoadCellTypeCount) {
    result = std::unexpected{ObjectPlacementError::kTooShort};
  }
  for (auto type = std::size_t{0}; result and type < kRoadCellTypeCount; ++type) {
    const auto offset = std::size_t{*ReadBigEndian<std::uint32_t>(bytes.subspan(4 * type))};
    const auto count_word = offset < file.size() ? ReadBigEndian<std::uint16_t>(bytes.subspan(offset)) : std::nullopt;
    const auto count =
        count_word and *count_word != kNoObjects ? static_cast<std::size_t>(*count_word & kCountMask) : std::size_t{0};
    if (not count_word or file.size() - offset - 2 < count * kEntryBytes) {
      result = std::unexpected{ObjectPlacementError::kListOutOfFile};
    } else {
      auto& objects = result->cell_types[type];
      for (auto index = std::size_t{0}; index < count; ++index) {
        auto entry = bytes.subspan(offset + 2 + (index * kEntryBytes), kEntryBytes);
        objects.push_back(PlacedObject{.x = *TakeBigEndian<std::int16_t>(entry),
                                       .y = *TakeBigEndian<std::int16_t>(entry),
                                       .z = *TakeBigEndian<std::int16_t>(entry),
                                       .type = *TakeBigEndian<std::uint16_t>(entry),
                                       .extra = *TakeBigEndian<std::uint16_t>(entry)});
      }
    }
  }
  return result;
}

}  // namespace hp2::host
