// `spellCheck` and `autoCorrect` as three states. See core/TextChecking.h.
//
// One rule, and it is the whole reason this is shared: unset is not false. A
// field that says nothing wants the platform's default, and resolving that to
// `false` would opt every ordinary field out of spell checking without the app
// ever asking.

#include "TestHarness.h"

#include "TextChecking.h"

#include <sstream>
#include <string>

using basalt::TextCheckingFlag;
using basalt::textCheckingFlag;
using basalt::textCheckingName;

TEST(text_checking_unset_is_its_own_answer) {
  EXPECT(textCheckingFlag(std::nullopt) == TextCheckingFlag::Unset);
  EXPECT(textCheckingFlag(true) == TextCheckingFlag::On);
  EXPECT(textCheckingFlag(false) == TextCheckingFlag::Off);
}

// The dump says nothing for a field that asked for nothing, rather than
// printing the platform's default as though the app had chosen it.
TEST(text_checking_an_unset_flag_has_no_name) {
  EXPECT(textCheckingName(TextCheckingFlag::Unset) == nullptr);
  EXPECT_EQ(std::string(textCheckingName(TextCheckingFlag::On)), std::string("on"));
  EXPECT_EQ(std::string(textCheckingName(TextCheckingFlag::Off)), std::string("off"));
}
