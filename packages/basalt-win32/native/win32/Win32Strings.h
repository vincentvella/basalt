// UTF-8 in, UTF-16 out, and back.
//
// Windows is a UTF-16 platform and React Native is a UTF-8 one, so this
// conversion sits on every boundary between them: DirectWrite takes wide
// strings, WIC's filenames are wide, and everything arriving from JavaScript
// through Fabric is UTF-8. Small enough to be tempting to write again in each
// file, which is how two of them end up disagreeing about what to do with a
// lone surrogate.

#pragma once

#include <windows.h>

#include <string>

namespace basalt::win32 {

inline std::wstring widen(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int needed =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), needed);
  return wide;
}

inline std::string narrow(const std::wstring &text) {
  if (text.empty()) {
    return {};
  }
  const int needed = WideCharToMultiByte(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string narrowed(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8,
                      0,
                      text.c_str(),
                      static_cast<int>(text.size()),
                      narrowed.data(),
                      needed,
                      nullptr,
                      nullptr);
  return narrowed;
}

} // namespace basalt::win32
