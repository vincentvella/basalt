// A font file the machine does not have, built from one it does.
//
// Shared by both suites' font tests, because both have the same problem. The
// case `expo-font` exists for is a family the system has never heard of: an app
// ships Inter, loads it at runtime, and `fontFamily: 'Inter'` has to start
// working. A test that registers a font already in the system index proves the
// name mapping and nothing else, because every layer underneath already knew
// that family -- which is exactly how GTK's Pango-context reload and macOS's
// real registration path came to have no test.
//
// So this takes a real font file and renames the family *inside* it, which is
// the one thing that makes it new. The rename is done in place, one byte per
// occurrence, so nothing in the file moves: a name record's length and every
// offset after it stay valid, and no checksum has to be recomputed. The
// replacement letter is applied to the family's first character wherever the
// name table spells it, in ASCII and in the UTF-16BE that Windows-platform
// name records use.
//
// Shipping a fixture font would be the other way to do this, and it would mean
// a binary in the repository plus its licence, for a file that would still have
// to be renamed to be sure the machine lacks it.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace basalt::test {

struct RenamedFont {
  // Empty when the source could not be read or has no name table to patch, so
  // a test can say so rather than assert on a file that was never written.
  std::string path;
  std::string family;
};

namespace detail {

inline std::uint16_t beU16(const unsigned char *at) {
  return static_cast<std::uint16_t>((at[0] << 8) | at[1]);
}

inline std::uint32_t beU32(const unsigned char *at) {
  return (static_cast<std::uint32_t>(at[0]) << 24) | (static_cast<std::uint32_t>(at[1]) << 16) |
      (static_cast<std::uint32_t>(at[2]) << 8) | static_cast<std::uint32_t>(at[3]);
}

// Replaces one byte in every occurrence of `needle` between `from` and `to`.
// `at` picks which byte of the match, which is 0 for ASCII and 1 for UTF-16BE,
// where the high byte of a Latin letter is zero.
inline int patchOccurrences(std::vector<unsigned char> &bytes,
                            std::size_t from,
                            std::size_t to,
                            const std::vector<unsigned char> &needle,
                            std::size_t at,
                            unsigned char replacement) {
  if (needle.empty() || to > bytes.size() || from >= to) {
    return 0;
  }
  int patched = 0;
  for (std::size_t i = from; i + needle.size() <= to; i++) {
    bool same = true;
    for (std::size_t j = 0; j < needle.size(); j++) {
      if (bytes[i + j] != needle[j]) {
        same = false;
        break;
      }
    }
    if (same) {
      bytes[i + at] = replacement;
      patched++;
      i += needle.size() - 1;
    }
  }
  return patched;
}

} // namespace detail

// Writes `source` to `destination` with `family`'s first letter replaced by
// `replacement` wherever the name table spells it, and answers with the new
// family name. `family` is the name the file calls itself, which is what the
// platform's own registry reads back.
inline RenamedFont renameFontFile(const std::string &source,
                                  const std::string &family,
                                  const std::string &destination,
                                  char replacement = 'Q') {
  if (family.empty() || family[0] == replacement) {
    return {};
  }

  std::FILE *in = std::fopen(source.c_str(), "rb");
  if (in == nullptr) {
    return {};
  }
  std::vector<unsigned char> bytes;
  unsigned char chunk[16384];
  while (const std::size_t read = std::fread(chunk, 1, sizeof(chunk), in)) {
    bytes.insert(bytes.end(), chunk, chunk + read);
  }
  std::fclose(in);

  // A collection holds several fonts and starts with its own header; the table
  // directory is not where this expects it, so say no rather than patch bytes
  // at a guess.
  if (bytes.size() < 12 || detail::beU32(bytes.data()) == 0x74746366 /* 'ttcf' */) {
    return {};
  }

  const std::uint16_t tables = detail::beU16(bytes.data() + 4);
  std::size_t nameAt = 0;
  std::size_t nameLength = 0;
  for (std::uint16_t i = 0; i < tables; i++) {
    const std::size_t record = 12 + std::size_t{16} * i;
    if (record + 16 > bytes.size()) {
      return {};
    }
    if (detail::beU32(bytes.data() + record) == 0x6E616D65 /* 'name' */) {
      nameAt = detail::beU32(bytes.data() + record + 8);
      nameLength = detail::beU32(bytes.data() + record + 12);
      break;
    }
  }
  if (nameAt == 0 || nameLength == 0 || nameAt + nameLength > bytes.size()) {
    return {};
  }

  const std::vector<unsigned char> ascii(family.begin(), family.end());
  std::vector<unsigned char> utf16;
  utf16.reserve(family.size() * 2);
  for (const char character : family) {
    utf16.push_back(0);
    utf16.push_back(static_cast<unsigned char>(character));
  }

  int patched = detail::patchOccurrences(bytes, nameAt, nameAt + nameLength, ascii, 0,
                                         static_cast<unsigned char>(replacement));
  patched += detail::patchOccurrences(bytes, nameAt, nameAt + nameLength, utf16, 1,
                                      static_cast<unsigned char>(replacement));
  if (patched == 0) {
    return {};
  }

  std::FILE *out = std::fopen(destination.c_str(), "wb");
  if (out == nullptr) {
    return {};
  }
  const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
  std::fclose(out);
  if (written != bytes.size()) {
    return {};
  }

  std::string renamed = family;
  renamed[0] = replacement;
  return RenamedFont{destination, renamed};
}

} // namespace basalt::test
