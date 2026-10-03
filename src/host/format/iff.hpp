#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "host/format/endian.hpp"

namespace hp2::host {

// A chunk identifier: four characters read as a big-endian long.
[[nodiscard]] constexpr std::uint32_t FourCc(std::string_view characters) {
  auto value = std::uint32_t{0};
  for (const auto letter : characters.substr(0, 4)) {
    value = (value << 8) | static_cast<std::uint8_t>(letter);
  }
  return value;
}

inline constexpr auto kFormSignature = FourCc("FORM");
inline constexpr auto k8SvxSignature = FourCc("8SVX");
inline constexpr auto kVoiceHeaderSignature = FourCc("VHDR");
inline constexpr auto kBodySignature = FourCc("BODY");
inline constexpr auto kIffFormHeaderBytes = std::size_t{12};  // "FORM", size, type
inline constexpr auto kIffChunkHeaderBytes = std::size_t{8};  // id, size

struct Chunk {
  std::uint32_t id{};
  std::uint32_t size{};
};

struct ChunkWithPayload {
  Chunk chunk;
  ConstDataSpan payload;
};

// True when `data` is a FORM of type `type`.
[[nodiscard]] inline bool IsIffForm(ConstDataSpan data, std::uint32_t type) {
  return data.size() >= kIffFormHeaderBytes and ReadBigEndian<std::uint32_t>(data) == kFormSignature and
         ReadBigEndian<std::uint32_t>(data.subspan(8)) == type;
}

// The chunks of a FORM's body, each with its payload; a chunk whose payload runs past the data
// ends the list. Chunks are padded to an even size.
[[nodiscard]] inline std::vector<ChunkWithPayload> GetIffChunks(ConstDataSpan data) {
  auto chunks = std::vector<ChunkWithPayload>{};
  auto complete = true;
  while (complete and data.size() >= kIffChunkHeaderBytes) {
    const auto chunk = Chunk{.id = *TakeBigEndian<std::uint32_t>(data), .size = *TakeBigEndian<std::uint32_t>(data)};
    complete = chunk.size <= data.size();
    if (complete) {
      chunks.push_back(ChunkWithPayload{.chunk = chunk, .payload = data.first(chunk.size)});
      const auto padded = std::size_t{chunk.size} + (chunk.size % 2);
      data = data.subspan(std::min(padded, data.size()));
    }
  }
  return chunks;
}

}  // namespace hp2::host
