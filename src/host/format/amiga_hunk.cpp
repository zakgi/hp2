#include "host/format/amiga_hunk.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "host/format/endian.hpp"

namespace hp2::host {

namespace {

// The two top bits of a size or block type select the memory type (chip, fast); layout ignores them.
constexpr auto kMemoryFlags = std::uint32_t{0xc0000000};
constexpr auto kLongBytes = std::size_t{4};

std::expected<std::uint32_t, HunkError> TakeLong(ConstDataSpan& data) {
  auto result = std::expected<std::uint32_t, HunkError>{std::unexpected{HunkError::kTruncated}};
  if (const auto value = TakeBigEndian<std::uint32_t>(data)) {
    result = *value;
  }
  return result;
}

// `count` longwords of `data`, consumed.
std::expected<ConstDataSpan, HunkError> TakeLongs(ConstDataSpan& data, std::uint32_t count) {
  auto result = std::expected<ConstDataSpan, HunkError>{std::unexpected{HunkError::kTruncated}};
  if (count <= data.size() / kLongBytes) {
    result = data.first(std::size_t{count} * kLongBytes);
    data = data.subspan(std::size_t{count} * kLongBytes);
  }
  return result;
}

// HUNK_RELOC32 body: {count, target hunk, offsets[count]}... ended by count 0.
std::expected<void, HunkError> ParseRelocations(ConstDataSpan& data, LoadableHunk& hunk) {
  auto result = std::expected<void, HunkError>{};
  auto count = TakeLong(data);
  while (count and *count != 0 and result) {
    const auto target = TakeLong(data);
    const auto offsets = target ? TakeLongs(data, *count) : std::unexpected{target.error()};
    if (offsets) {
      auto& list = hunk.relocations[*target];
      for (auto index = std::size_t{0}; index < *count; ++index) {
        list.push_back(*ReadBigEndian<std::uint32_t>(offsets->subspan(index * kLongBytes)));
      }
      count = TakeLong(data);
    } else {
      result = std::unexpected{offsets.error()};
    }
  }
  if (result and not count) {
    result = std::unexpected{count.error()};
  }
  return result;
}

// HUNK_SYMBOL body: {name length in longwords, name, offset}... ended by length 0.
std::expected<void, HunkError> ParseSymbols(ConstDataSpan& data, LoadableHunk& hunk) {
  auto result = std::expected<void, HunkError>{};
  auto length = TakeLong(data);
  while (length and *length != 0 and result) {
    const auto name = TakeLongs(data, *length);
    const auto offset = name ? TakeLong(data) : std::unexpected{name.error()};
    if (offset) {
      auto text = std::string_view{reinterpret_cast<const char*>(name->data()), name->size()};
      text = text.substr(0, text.find('\0'));
      hunk.symbols.emplace(text, *offset);
      length = TakeLong(data);
    } else {
      result = std::unexpected{offset.error()};
    }
  }
  if (result and not length) {
    result = std::unexpected{length.error()};
  }
  return result;
}

// One CODE/DATA/BSS block and its trailing blocks up to HUNK_END.
std::expected<LoadableHunk, HunkError> ParseHunk(ConstDataSpan& data, std::uint32_t size) {
  auto hunk = LoadableHunk{.size = size};
  auto result = std::expected<void, HunkError>{};
  const auto type = TakeLong(data);
  const auto size_longs = type ? TakeLong(data) : std::unexpected{type.error()};
  if (not size_longs) {
    result = std::unexpected{size_longs.error()};
  } else if (std::size_t{*size_longs & ~kMemoryFlags} * kLongBytes > size) {
    result = std::unexpected{HunkError::kSizeMismatch};
  } else {
    hunk.type = static_cast<HunkType>(*type & ~kMemoryFlags);
    if (hunk.type == HunkType::kCode or hunk.type == HunkType::kData) {
      const auto bytes = TakeLongs(data, *size_longs & ~kMemoryFlags);
      if (bytes) {
        hunk.data = *bytes;
      } else {
        result = std::unexpected{bytes.error()};
      }
    } else if (hunk.type != HunkType::kBss) {
      result = std::unexpected{HunkError::kUnsupportedBlock};
    }
  }
  auto ended = false;
  while (result and not ended) {
    const auto block = TakeLong(data);
    if (not block) {
      result = std::unexpected{block.error()};
    } else {
      switch (static_cast<HunkType>(*block & ~kMemoryFlags)) {
        case HunkType::kReloc32:
          result = ParseRelocations(data, hunk);
          break;
        case HunkType::kSymbols:
          result = ParseSymbols(data, hunk);
          break;
        case HunkType::kEnd:
          ended = true;
          break;
        default:
          result = std::unexpected{HunkError::kUnsupportedBlock};
          break;
      }
    }
  }
  return result.transform([&hunk] { return std::move(hunk); });
}

}  // namespace

std::expected<HunkFile, HunkError> HunkFile::FromBytes(ConstDataSpan file) {
  auto data = file;
  auto result = std::expected<HunkFile, HunkError>{HunkFile{}};
  const auto header = TakeLong(data);
  const auto libraries = header ? TakeLong(data) : std::unexpected{header.error()};
  const auto table_size = libraries ? TakeLong(data) : std::unexpected{libraries.error()};
  const auto first = table_size ? TakeLong(data) : std::unexpected{table_size.error()};
  const auto last = first ? TakeLong(data) : std::unexpected{first.error()};
  if (not header or *header != std::to_underlying(HunkType::kHeader)) {
    result = std::unexpected{HunkError::kNotExecutable};
  } else if (not last) {
    result = std::unexpected{last.error()};
  } else if (*libraries != 0) {
    // Resident library names only occur in overlaid or library files, not in executables.
    result = std::unexpected{HunkError::kResidentLibraries};
  } else if (*last < *first or *last - *first + 1 != *table_size) {
    result = std::unexpected{HunkError::kSizeMismatch};
  } else {
    const auto sizes = TakeLongs(data, *table_size);
    if (not sizes) {
      result = std::unexpected{sizes.error()};
    }
    for (auto index = std::size_t{0}; result and index < *table_size; ++index) {
      const auto size =
          (*ReadBigEndian<std::uint32_t>(sizes->subspan(index * kLongBytes)) & ~kMemoryFlags) * kLongBytes;
      auto hunk = ParseHunk(data, static_cast<std::uint32_t>(size));
      if (hunk) {
        result->hunks.push_back(std::move(*hunk));
      } else {
        result = std::unexpected{hunk.error()};
      }
    }
  }
  return result;
}

}  // namespace hp2::host
