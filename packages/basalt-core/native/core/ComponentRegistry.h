// Which components a platform supports — a seam, not a list.
//
// `getDefaultComponentRegistryFactory()` is declared by ReactCommon and
// deliberately *not* defined there: no .cpp in the whole tree implements it.
// Each host supplies its own, and in doing so declares what its platform can
// actually put on screen. Fantom has a version registering the full set.
//
// This used to be one shared definition, on the reasonable-looking grounds that
// a descriptor is portable C++ and registering one costs nothing. Linking the
// macOS mounting manager disproved that. Registering ParagraphComponentDescriptor
// constructs a `TextLayoutManager`, whose implementation this build deliberately
// replaces per platform -- Pango on Linux -- so a platform that has no text
// engine yet does not fail to render text, it fails to link. Which is the right
// failure, and is why the registry belongs beside the platform rather than in
// core.
//
// So each platform defines this function in its own translation unit:
//
//   gtk/ComponentRegistryGtk.cpp   View, Paragraph, Text, RawText, Image,
//                                  ScrollView, TextInput
//   mac/ComponentRegistryAppKit.mm    View
//
// and the set it registers must match what that platform's mounting manager
// answers `hasComponent` for. They are two statements of the same fact, and
// when they disagree the registry wins and builds shadow nodes nothing can
// mount -- which renders as blank rectangles rather than as an error.

#pragma once

#include <react/renderer/componentregistry/ComponentDescriptorFactory.h>
