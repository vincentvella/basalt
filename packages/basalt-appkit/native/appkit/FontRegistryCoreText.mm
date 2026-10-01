// The font seam on macOS, backed by Core Text.
//
// The counterpart of gtk/FontRegistryFontconfig.cpp, and the first seam phase
// 17 identified: `expo-font` hands over a file and a name of the app's choosing
// -- "Inter", say -- and expects `fontFamily: 'Inter'` to work afterwards. A
// font file carries its own family name inside it, usually something else
// entirely, and Core Text indexes by that. So loading the file is only half the
// job; the other half is remembering that this app calls it "Inter".
//
// Registration is process-scoped: the font becomes available to this process
// and to nothing else on the machine, which is what an app loading a bundled
// font wants and the only scope that needs no user consent.
//
// Nothing on this platform renders text yet -- there is no Core Text
// TextLayoutManager, which is the seam phase 19 found. So `resolveFontFamily`
// currently has no reader. It is implemented anyway, because the alternative is
// a registry that silently records nothing and a font milestone that starts by
// debugging this file.

#include "FontRegistry.h"

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <mutex>
#include <unordered_map>
#include <vector>

namespace basalt {

namespace {

struct Registry {
  std::mutex mutex;
  unsigned long generation{0};
  // What the app calls it -> what the file calls itself.
  std::unordered_map<std::string, std::string> families;
  std::vector<std::string> order;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

// The family name recorded inside the file. Core Text will register a file and
// then index it under a name the app has never heard of, so this is read back
// rather than assumed -- the same reason the fontconfig side reads it.
//
// A file can hold several faces; the first one's family is used, which matches
// what a single-font asset means and is what fontconfig's equivalent does.
std::string familyInFile(NSURL *url) {
  NSArray *descriptors =
      (__bridge_transfer NSArray *)CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)url);
  if (descriptors.count == 0) {
    return {};
  }

  CTFontDescriptorRef descriptor = (__bridge CTFontDescriptorRef)descriptors[0];
  NSString *family = (__bridge_transfer NSString *)CTFontDescriptorCopyAttribute(
      descriptor, kCTFontFamilyNameAttribute);
  if (family == nil) {
    return {};
  }
  return family.UTF8String;
}

} // namespace

bool registerFont(const std::string &name, const std::string &path) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);

  if (state.families.find(name) != state.families.end()) {
    return true;
  }

  @autoreleasepool {
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    if (url == nil) {
      return false;
    }

    CFErrorRef error = nullptr;
    if (!CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url,
                                          kCTFontManagerScopeProcess,
                                          &error)) {
      // Already registered is not a failure: two apps' worth of the same file,
      // or a second call for a different app-facing name, both land here and
      // both leave the font usable.
      const bool alreadyRegistered =
          error != nullptr && CFErrorGetCode(error) == kCTFontManagerErrorAlreadyRegistered;
      if (error != nullptr) {
        CFRelease(error);
      }
      if (!alreadyRegistered) {
        return false;
      }
    }

    const std::string family = familyInFile(url);
    if (family.empty()) {
      // Registered but unusable: without the real family there is no way to ask
      // for it, and pretending otherwise would fail later and further away.
      return false;
    }

    state.families.emplace(name, family);
    state.order.push_back(name);
    state.generation++;
  }

  return true;
}

unsigned long fontGeneration() {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  return state.generation;
}

std::string resolveFontFamily(const std::string &name) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  const auto it = state.families.find(name);
  return it == state.families.end() ? name : it->second;
}

bool isFontRegistered(const std::string &name) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  return state.families.find(name) != state.families.end();
}

std::string registeredFontNamesJoined(char separator) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  std::string joined;
  for (const auto &name : state.order) {
    if (!joined.empty()) {
      joined.push_back(separator);
    }
    joined += name;
  }
  return joined;
}

} // namespace basalt
