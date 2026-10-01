// AttributedString -> RnWin32TextLayout, and nothing else.
//
// Separate from RnWin32TextLayout.h for the reason that file states: the view
// layer draws paragraphs and must not gain a React Native dependency to do it.
// This is the file that does know about React Native, and it is the only place
// a `TextAttributes` is turned into a `RnTextStyle`.
//
// Shared by measurement and painting -- `DirectWriteLayoutManager.cpp` calls it
// from `TextLayoutManager::measure`, and `Win32MountingManager` calls it when a
// Paragraph's state arrives. That sharing is the point: if the two built
// layouts differently, Yoga would allot a box computed one way and the view
// would paint text laid out another. `docs/DECISIONS.md` makes the same
// argument for Pango.

#pragma once

#include "RnWin32TextLayout.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>

#include <memory>

namespace basalt::win32 {

// One fragment's attributes, as this platform understands them. Exposed so the
// mounting manager can ask the same question about a lone run without building
// a whole paragraph.
RnTextStyle buildTextStyle(const facebook::react::TextAttributes &textAttributes);

std::shared_ptr<RnWin32TextLayout>
buildTextLayout(const facebook::react::AttributedString &attributedString,
                const facebook::react::ParagraphAttributes &paragraphAttributes);

} // namespace basalt::win32
