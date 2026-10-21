#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "host/format/endian.hpp"

namespace hp2::host {

// AmigaDOS load-file block types (HUNK_*).
// Keep the file format's 32-bit hunk tag representation.
// NOLINTNEXTLINE(performance-enum-size)
enum class HunkType : std::uint32_t {
  Header = 0x3f3,
  Code = 0x3e9,
  Data = 0x3ea,
  Bss = 0x3eb,
  Reloc32 = 0x3ec,
  Symbols = 0x3f0,
  End = 0x3f2,
};

enum class HunkError : std::uint8_t {
  Truncated,
  NotExecutable,
  ResidentLibraries,
  SizeMismatch,
  UnsupportedBlock,
};

// One code, data or BSS hunk with the blocks that follow it up to HUNK_END. Data is read
// unrelocated: a longword the loader would relocate holds an offset into its target hunk (the
// target is in `relocations`), which is how pointer tables are followed.
struct LoadableHunk {
  HunkType type{HunkType::Code};
  // Size in memory, from the header's size table; BSS hunks have it without data in the file.
  std::uint32_t size{};
  // The hunk's bytes in the file (empty for BSS); may be shorter than `size`.
  ConstDataSpan data;
  // Target hunk index -> offsets in this hunk of longwords to relocate by that hunk's address.
  std::map<std::uint32_t, std::vector<std::uint32_t>> relocations;
  // HUNK_SYMBOL names (views into the file) -> offsets in this hunk.
  std::map<std::string_view, std::uint32_t> symbols;

  // `length` bytes at `offset`; empty when they are not all in the file's data.
  [[nodiscard]] std::optional<ConstDataSpan> Bytes(std::uint32_t offset, std::size_t length) const {
    auto bytes = std::optional<ConstDataSpan>{};
    if (offset <= data.size() and length <= data.size() - offset) {
      bytes = data.subspan(offset, length);
    }
    return bytes;
  }

  // The big-endian T at `offset`; empty when it is not in the file's data.
  template <std::integral T>
  [[nodiscard]] std::optional<T> Read(std::uint32_t offset) const {
    const auto bytes = Bytes(offset, sizeof(T));
    return bytes ? ReadBigEndian<T>(*bytes) : std::nullopt;
  }
};

// An AmigaDOS executable's hunks, as views into the file bytes (which must outlive it).
struct HunkFile {
  std::vector<LoadableHunk> hunks;

  // Parses HUNK_HEADER and every hunk it declares: CODE, DATA or BSS, then RELOC32 and SYMBOL
  // blocks up to HUNK_END.
  [[nodiscard]] static std::expected<HunkFile, HunkError> FromBytes(ConstDataSpan file);
};

// A place in the executable: hunk index and byte offset in it, written hunk:offset in comments and
// documents (1:2870).
struct HunkOffset {
  std::uint32_t hunk{};
  std::uint32_t offset{};
};

}  // namespace hp2::host
