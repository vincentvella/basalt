#include "TextDirection.h"

#include "TextAlignments.h"
#include "WritingDirections.h"

#include <cstdint>
#include <string>

namespace basalt {

namespace {

// One code point from UTF-8, advancing `index`. Returns 0 for a byte that
// cannot start a sequence, which the caller treats as neutral -- a layout
// decision rather than a validation.
std::uint32_t nextCodePoint(std::string_view text, std::size_t &index) {
  const auto byte = static_cast<unsigned char>(text[index]);
  std::size_t length = 1;
  std::uint32_t value = byte;

  if (byte >= 0xF0) {
    length = 4;
    value = byte & 0x07U;
  } else if (byte >= 0xE0) {
    length = 3;
    value = byte & 0x0FU;
  } else if (byte >= 0xC0) {
    length = 2;
    value = byte & 0x1FU;
  } else if (byte >= 0x80) {
    // A continuation byte with no lead. Skip it.
    index += 1;
    return 0;
  }

  if (index + length > text.size()) {
    index = text.size();
    return 0;
  }
  for (std::size_t offset = 1; offset < length; ++offset) {
    const auto continuation = static_cast<unsigned char>(text[index + offset]);
    if ((continuation & 0xC0U) != 0x80U) {
      index += offset;
      return 0;
    }
    value = (value << 6) | (continuation & 0x3FU);
  }
  index += length;
  return value;
}

// The characters inside the Arabic block that are *numbers* rather than
// letters: Arabic-Indic digits are class AN and the extended ones are class EN,
// and neither is strong. Reading the block as a whole is the obvious mistake
// and it makes "١٢٣ hello" a right-to-left paragraph, which Unicode says it is
// not. Asked by both halves below, which is why it is its own function: the
// first version excluded them from the strong test and forgot the neutral one,
// so a digit stopped the search as though it were a Latin letter.
bool isArabicNumber(std::uint32_t code) {
  return (code >= 0x0660 && code <= 0x0669) || code == 0x066B || code == 0x066C ||
      (code >= 0x06F0 && code <= 0x06F9);
}

// The blocks whose letters are class R or AL. Written as ranges because that is
// how Unicode allocates them, and ordered so a reader can check them against
// the standard's block list.
bool isStrongRightToLeft(std::uint32_t code) {
  // Hebrew, and the two marks that are class R in their own right.
  if (code == 0x200F /* RLM */ || code == 0x061C /* ALM */) {
    return true;
  }
  if (code >= 0x0590 && code <= 0x05FF) {
    return true;
  }
  // Arabic, minus its numbers; see isArabicNumber.
  if (code >= 0x0600 && code <= 0x06FF) {
    return !isArabicNumber(code);
  }
  // Syriac, Arabic Supplement, Thaana, NKo, Samaritan, Mandaic, Syriac
  // Supplement, Arabic Extended-B and -A, in one range: U+0700..U+08FF is
  // right-to-left throughout.
  if (code >= 0x0700 && code <= 0x08FF) {
    return true;
  }
  // Hebrew and Arabic presentation forms.
  if (code >= 0xFB1D && code <= 0xFDFF) {
    return true;
  }
  if (code >= 0xFE70 && code <= 0xFEFF) {
    return true;
  }
  // The historical right-to-left scripts: Cypriot through Arabic Extended-C.
  if (code >= 0x10800 && code <= 0x10FFF) {
    return true;
  }
  if (code >= 0x1E800 && code <= 0x1EFFF) {
    return true;
  }
  return false;
}

// Everything that is not a letter of some script: skipped, because P2 asks for
// the first *strong* character.
//
// Spelled as what to skip rather than as what counts, because what counts is
// every letter of every left-to-right script and no list of those is short.
// What is skipped is ASCII punctuation and digits, the Latin-1 punctuation
// block, combining marks, general punctuation and the block of currency and
// symbol characters after it -- which is what sits in front of a word in real
// text.
bool isNeutral(std::uint32_t code) {
  if (code == 0) {
    return true;
  }
  if (isArabicNumber(code)) {
    return true;
  }
  if (code < 0x0041) {
    // Controls, space, digits and the punctuation before 'A'.
    return true;
  }
  if (code >= 0x005B && code <= 0x0060) {
    return true;
  }
  if (code >= 0x007B && code <= 0x00BF) {
    // ASCII punctuation past 'z', the C1 controls, and Latin-1's punctuation
    // and symbols, which end before À.
    return true;
  }
  if (code == 0x00D7 || code == 0x00F7) {
    // Multiplication and division signs, which sit among the Latin letters.
    return true;
  }
  if (code >= 0x0300 && code <= 0x036F) {
    // Combining marks. A letter carrying one is strong; the mark is not.
    return true;
  }
  if (code >= 0x2000 && code <= 0x2BFF) {
    // General punctuation through the symbol blocks. The two right-to-left
    // marks in here are answered before this is asked.
    return true;
  }
  if (code >= 0x1F000 && code <= 0x1FAFF) {
    // Emoji and the symbol planes.
    return true;
  }
  return false;
}

} // namespace

bool textStartsRightToLeft(std::string_view utf8) {
  std::size_t index = 0;
  while (index < utf8.size()) {
    const std::uint32_t code = nextCodePoint(utf8, index);
    if (isStrongRightToLeft(code)) {
      return true;
    }
    if (!isNeutral(code)) {
      // The first strong left-to-right character, which ends the search: P2
      // takes the first strong character of either kind.
      return false;
    }
  }
  return false;
}

bool paragraphIsRightToLeft(std::optional<facebook::react::WritingDirection> direction,
                            std::string_view utf8) {
  if (direction.has_value()) {
    switch (*direction) {
      case facebook::react::WritingDirection::LeftToRight:
        return false;
      case facebook::react::WritingDirection::RightToLeft:
        return true;
      case facebook::react::WritingDirection::Natural:
        break;
    }
  }
  return textStartsRightToLeft(utf8);
}

const char *paragraphTextAlignmentName(
    const facebook::react::AttributedString &attributedString) {
  const auto &fragments = attributedString.getFragments();
  const std::optional<facebook::react::TextAlignment> alignment = fragments.empty()
      ? std::optional<facebook::react::TextAlignment>{}
      : fragments.front().textAttributes.alignment;
  const bool rightToLeft = paragraphIsRightToLeft(writingDirection(attributedString),
                                                  paragraphText(attributedString));
  return physicalTextAlignmentName(physicalTextAlignment(alignment, rightToLeft));
}

std::string paragraphText(const facebook::react::AttributedString &attributedString) {
  std::string text;
  for (const auto &fragment : attributedString.getFragments()) {
    text += fragment.string;
  }
  return text;
}

} // namespace basalt
