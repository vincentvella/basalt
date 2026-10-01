// Fonts an app loads at runtime, and the name it knows them by.
//
// Declared here and implemented per platform, because this is one of the places
// a desktop platform genuinely differs: fontconfig on Linux, and something else
// on macOS or Windows. Core calls it -- `expo-font` arrives here -- and links
// against whichever implementation the platform half provides. Linux's is
// src/FontRegistryFontconfig.cpp.
//
// `expo-font` hands over a file and a name of the app's choosing -- "Inter",
// say -- and expects `fontFamily: 'Inter'` to work afterwards. A font file
// carries its own family name inside it, which is usually something else
// entirely, and fontconfig indexes by that. So loading the file is only half
// the job; the other half is remembering that this app calls it "Inter".
//
// Kept here rather than in the Expo layer because the Pango side has to consult
// it on every text measurement, and because nothing about it is Expo-specific:
// any future API that loads a font at runtime wants exactly this.

#pragma once

#include <string>

namespace basalt {

// Makes a font file available to Pango, under `name`.
//
// Returns false if the file cannot be read or fontconfig will not take it, in
// which case nothing is registered. Safe to call twice with the same name.
bool registerFont(const std::string &name, const std::string &path);

// The family Pango should be asked for, given what the app called it. Returns
// `name` unchanged when it was never registered, so ordinary system families
// pass straight through.
std::string resolveFontFamily(const std::string &name);

// Bumped every time a font is registered. Text measured before a font arrived
// was measured against a different set of fonts, and a cache that does not know
// that will keep handing back the old size -- which is what makes a font loaded
// after first render appear to do nothing.
unsigned long fontGeneration();

// Whether `name` has been registered. expo-font insists this exists and is a
// function, and throws when it is not, so it is not optional however web-only
// its type declaration claims to be.
bool isFontRegistered(const std::string &name);

// The names registered so far, in registration order, for
// ExpoFontLoader.getLoadedFonts.
std::string registeredFontNamesJoined(char separator);

} // namespace basalt
