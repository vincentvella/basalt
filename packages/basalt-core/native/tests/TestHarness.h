// A very small test harness.
//
// Deliberately not googletest. React Native vendors its own copy for its own
// tests, and pulling that into this build would mean another third-party tree
// to pin and another way for the build to break on an upstream bump. Everything
// below is about sixty lines and has no dependencies.
//
// Tests self-register through the TEST macro and run in registration order.
// A failed check records a message and keeps going, so one broken assertion
// does not hide the rest of the test's findings.

#pragma once

// If glog's macros are visible, make sure nothing here quietly binds to them.
#if defined(CHECK_EQ) && !defined(BASALT_TEST_HARNESS_ALLOW_GLOG_CHECK)
// Intentionally left alone: glog owns CHECK/CHECK_EQ. This header defines
// EXPECT/EXPECT_EQ/EXPECT_NEAR instead, which glog does not use.
#endif

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace basalt::testing {

struct TestCase {
  std::string name;
  std::function<void()> body;
};

// Registry and current-test state. A single translation unit owns these; see
// TestHarness.cpp.
std::vector<TestCase> &registry();
void recordFailure(const std::string &where, const std::string &what);
// Runs the registered tests. Any arguments are substrings of test names: a test
// runs if it matches one of them, and `--list` prints the names and exits.
int runAllTests(int argc = 0, char **argv = nullptr);

struct Registrar {
  Registrar(const char *name, std::function<void()> body) {
    registry().push_back(TestCase{name, std::move(body)});
  }
};

} // namespace basalt::testing

#define TEST(name)                                                                                 \
  static void name();                                                                              \
  static ::basalt::testing::Registrar registrar_##name(#name, name);                              \
  static void name()

// Named EXPECT rather than CHECK because glog -- which React Native pulls in
// through almost every header -- already defines CHECK and CHECK_EQ. Those win
// the preprocessor fight silently, and a failing assertion then aborts the
// process instead of being recorded, taking every later test with it.
#define EXPECT(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      ::basalt::testing::recordFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__),     \
                                        "expected " #condition);                                   \
    }                                                                                              \
  } while (false)

#define EXPECT_EQ(actual, expected)                                                                 \
  do {                                                                                             \
    const auto actualValue = (actual);                                                             \
    const auto expectedValue = (expected);                                                         \
    if (!(actualValue == expectedValue)) {                                                         \
      std::ostringstream message;                                                                  \
      message << "expected " #actual " == " #expected " but got " << actualValue << " vs "         \
              << expectedValue;                                                                    \
      ::basalt::testing::recordFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__),     \
                                        message.str());                                            \
    }                                                                                              \
  } while (false)

#define EXPECT_NEAR(actual, expected, tolerance)                                                    \
  do {                                                                                             \
    const double actualValue = static_cast<double>(actual);                                        \
    const double expectedValue = static_cast<double>(expected);                                    \
    if (std::fabs(actualValue - expectedValue) > (tolerance)) {                                    \
      std::ostringstream message;                                                                  \
      message << "expected " #actual " ~= " << expectedValue << " but got " << actualValue;         \
      ::basalt::testing::recordFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__),     \
                                        message.str());                                            \
    }                                                                                              \
  } while (false)
