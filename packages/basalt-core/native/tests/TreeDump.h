// Reading a field back out of a host's tree dump.
//
// The three hosts print the same tree in the same format -- that is what
// scripts/compare_hosts.sh rests on -- so a test that wants one number out of
// it should not have to know which host wrote it. This is here rather than
// three times over for the same reason the dump's field order is written down
// three times over: the moment they drift, the comparison is worthless.

#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <string>

namespace basalt::testing {

// The six numbers of `transform=(a,b,c,d,tx,ty)` for one tag: the 2D affine
// part, in the order CSS writes a matrix(). All zeroes when that tag printed
// no transform at all, which is what an identity looks like in this dump --
// distinguishable from a real matrix, since a real one never has a zero a
// and d together.
inline std::array<double, 6> transformIn(const std::string &tree, int tag) {
  std::array<double, 6> found{};
  std::istringstream lines(tree);
  std::string line;
  const std::string needle = "tag=" + std::to_string(tag) + " ";
  while (std::getline(lines, line)) {
    if (line.find(needle) == std::string::npos) {
      continue;
    }
    const auto at = line.find("transform=(");
    if (at == std::string::npos) {
      return found;
    }
    std::string numbers = line.substr(at + std::strlen("transform=("));
    numbers = numbers.substr(0, numbers.find(')'));
    std::replace(numbers.begin(), numbers.end(), ',', ' ');
    std::istringstream parts(numbers);
    for (auto &value : found) {
      parts >> value;
    }
    return found;
  }
  return found;
}

} // namespace basalt::testing
