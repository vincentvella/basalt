// The runner itself, with no toolkit in it.
//
// The toolkit lives in the `main` beside this file -- GTK has to be initialised
// before a widget can be constructed, and AppKit has its own requirements -- so
// each platform brings its own entry point and shares everything below.

#include "TestHarness.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace basalt::testing {

namespace {

struct CurrentTest {
  std::string name;
  std::vector<std::string> failures;
};

CurrentTest &current() {
  static CurrentTest test;
  return test;
}

} // namespace

std::vector<TestCase> &registry() {
  static std::vector<TestCase> cases;
  return cases;
}

void recordFailure(const std::string &where, const std::string &what) {
  current().failures.push_back(where + ": " + what);
}

namespace {

std::string lowered(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

} // namespace

int runAllTests(int argc, char **argv) {
  // Arguments are substrings of test names, and a test runs if it matches any
  // of them. There is a lot of the day in the difference between running one
  // test and running all of them, and this runner used to accept an argument
  // and silently ignore it -- which is worse than rejecting one, because it
  // looks like the filter worked and every test passed.
  std::vector<std::string> wanted;
  for (int i = 1; i < argc; i++) {
    const std::string argument = argv[i];
    if (argument == "--list") {
      for (const auto &test : registry()) {
        std::cout << test.name << "\n";
      }
      return 0;
    }
    wanted.push_back(lowered(argument));
  }

  std::vector<TestCase> selected;
  for (const auto &test : registry()) {
    const std::string name = lowered(test.name);
    if (wanted.empty() ||
        std::any_of(wanted.begin(), wanted.end(), [&name](const std::string &needle) {
          return name.find(needle) != std::string::npos;
        })) {
      selected.push_back(test);
    }
  }

  if (selected.empty()) {
    std::cout << "no test matches; --list prints the names\n";
    return 1;
  }

  int failed = 0;

  std::cout << "running " << selected.size() << " tests\n";

  for (const auto &test : selected) {
    current().name = test.name;
    current().failures.clear();

    test.body();

    if (current().failures.empty()) {
      std::cout << "  ok    " << test.name << "\n";
    } else {
      ++failed;
      std::cout << "  FAIL  " << test.name << "\n";
      for (const auto &failure : current().failures) {
        std::cout << "        " << failure << "\n";
      }
    }
  }

  std::cout << "\n" << selected.size() - static_cast<size_t>(failed) << "/" << selected.size()
            << " passed\n";
  return failed == 0 ? 0 : 1;
}

} // namespace basalt::testing
