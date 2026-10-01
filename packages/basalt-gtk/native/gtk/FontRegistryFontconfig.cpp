#include "FontRegistry.h"

#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include <fontconfig/fontconfig.h>
// FcFreeTypeQuery lives here rather than in fontconfig.h, and it is what reads
// the family name out of the file.
#include <fontconfig/fcfreetype.h>

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

// The family name recorded inside the file. fontconfig will happily add a file
// and then index it under a name the app has never heard of, so this is read
// back rather than assumed.
std::string familyInFile(const std::string &path) {
  FcBlanks *blanks = FcConfigGetBlanks(FcConfigGetCurrent());
  int count = 0;
  FcPattern *pattern =
      FcFreeTypeQuery(reinterpret_cast<const FcChar8 *>(path.c_str()), 0, blanks, &count);
  if (pattern == nullptr) {
    return {};
  }

  FcChar8 *family = nullptr;
  std::string result;
  if (FcPatternGetString(pattern, FC_FAMILY, 0, &family) == FcResultMatch && family != nullptr) {
    result = reinterpret_cast<const char *>(family);
  }
  FcPatternDestroy(pattern);
  return result;
}

} // namespace

bool registerFont(const std::string &name, const std::string &path) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);

  if (state.families.find(name) != state.families.end()) {
    return true;
  }

  if (FcInit() == FcFalse) {
    return false;
  }
  if (FcConfigAppFontAddFile(FcConfigGetCurrent(),
                             reinterpret_cast<const FcChar8 *>(path.c_str())) == FcFalse) {
    return false;
  }

  const std::string family = familyInFile(path);
  if (family.empty()) {
    // Added to fontconfig but unusable: without the real family there is no way
    // to ask Pango for it, and pretending otherwise would fail later and
    // further away.
    return false;
  }

  state.families.emplace(name, family);
  state.order.push_back(name);
  state.generation++;

  // Pango caches which fonts exist. Adding one to fontconfig behind its back
  // leaves it looking at the old list, so the family would not be found until
  // something else happened to invalidate it. Only meaningful for a fontconfig
  // backed map, which is not the only kind Pango has.
  PangoFontMap *fontMap = pango_cairo_font_map_get_default();
  if (PANGO_IS_FC_FONT_MAP(fontMap)) {
    pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(fontMap));
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
