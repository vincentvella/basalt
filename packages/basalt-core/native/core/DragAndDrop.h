// Dragging something into the application, and out of it.
//
// A desktop application is expected to accept a file dropped on it. React
// Native has no API for that, because a phone has no pointer to drag with, so
// there is nothing here to be compatible with -- which makes the shape this
// takes a decision rather than a port.
//
// ## Marked by nativeID, like the title bar's drag regions
//
// A view says it accepts a drop by setting `nativeID`, the one prop a plain
// `<View>` carries to the host on every platform. The alternative is a
// registration call keyed on a tag from `findNodeHandle`, which means a
// module method, a ref, and a lifetime to get wrong -- for a fact about a
// view that React already sends down on every commit.
//
// See core/TitleBarRegions.h, which marks views the same way and for the same
// reason. The strings live here rather than in a host because every host that
// reads them must read the same ones, and JavaScript writes them from a
// fourth place.
//
// ## What a drop target accepts
//
// Files and text, which are the two kinds every desktop agrees on. The marker
// carries them so that a view accepting only files is not told about dragged
// text at all -- the desktop asks "will you take this?" before the drop, and
// answering honestly is what makes the cursor say no rather than the app
// rejecting it afterwards.
//
// ## Which view is told
//
// The deepest accepting view under the pointer, found by walking up from the
// view the hit test returned. Exactly the rule the title bar applies, and for
// the same reason: a label inside a drop target should not have to be marked,
// and a drop target inside another should win.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <react/renderer/core/ReactPrimitives.h>

namespace basalt {

// What a drop carries. Both may be present: a file manager commonly offers a
// dragged file as a path *and* as its name in text, and which one an app wants
// is the app's business.
struct DragPayload {
  // Absolute paths. Not URIs: every host is asked for a file list and hands
  // back something path-shaped, and an app that wanted a URI can make one.
  std::vector<std::string> files;
  std::string text;

  bool hasFiles() const { return !files.empty(); }
  bool hasText() const { return !text.empty(); }
  bool empty() const { return files.empty() && text.empty(); }
};

// What a view will take. A mask rather than an enum: a view may accept both,
// and "either" is the common case.
enum DropAccepts : std::uint16_t {
  DropAcceptsNone = 0,
  DropAcceptsFiles = 1 << 0,
  DropAcceptsText = 1 << 1,
};

// The prefix a marked view's nativeID starts with. What follows is the
// accepted kinds, comma separated: `basalt-drop:files`, `basalt-drop:text`,
// `basalt-drop:files,text`.
//
// A prefix rather than three fixed strings, because this one has a payload and
// the title bar's does not.
inline constexpr const char *kDropTargetIdPrefix = "basalt-drop:";

// What a nativeID says this view accepts, or DropAcceptsNone when it is not a
// drop marker at all -- which is every other nativeID an app sets, including
// the title bar's and its own.
//
// Unknown kinds are ignored rather than refused: a marker written by a newer
// JavaScript than the host should degrade to the kinds this host knows, not to
// nothing.
inline std::uint16_t dropAcceptsFrom(const std::string &nativeId) {
  const std::string prefix(kDropTargetIdPrefix);
  if (nativeId.rfind(prefix, 0) != 0) {
    return DropAcceptsNone;
  }
  std::uint16_t accepts = DropAcceptsNone;
  std::string kind;
  // One pass, splitting on commas, with the last kind flushed at the end.
  for (size_t i = prefix.size(); i <= nativeId.size(); i++) {
    if (i == nativeId.size() || nativeId[i] == ',') {
      if (kind == "files") {
        accepts |= DropAcceptsFiles;
      } else if (kind == "text") {
        accepts |= DropAcceptsText;
      }
      kind.clear();
    } else {
      kind += nativeId[i];
    }
  }
  return accepts;
}

// What a `<TextInput acceptDragAndDropTypes={[...]}>` will take.
//
// React Native's prop is a list of types in two vocabularies -- it documents
// Android MIME types and iOS uniform type identifiers, and an app written for
// both sends both -- so both are read here. Mapped onto the two kinds this
// platform's drop targets speak rather than carried through, because a host
// here asks "files or text?" and nothing below this answers in MIME.
//
// Android's implementation is the reason this is a filter and not a
// registration: `ReactEditText.onDragEvent` refuses a drag whose
// `clipDescription` matches none of the list and otherwise hands it to the
// platform. A field with no list is unchanged, which is why the absent case
// answers DropAcceptsNone and the caller falls back to the view's own marker.
//
// A type nobody here recognises is ignored rather than refused, for the reason
// `dropAcceptsFrom` ignores an unknown kind: a list written for a phone should
// degrade to what this desktop knows.
inline std::uint16_t dropAcceptsFromTypes(const std::vector<std::string> &types) {
  std::uint16_t accepts = DropAcceptsNone;
  for (const std::string &type : types) {
    // Text, in both vocabularies: Android's "text/plain" and the wildcard
    // "text/*", and Apple's "public.plain-text", "public.utf8-plain-text" and
    // the "public.text" they conform to.
    if (type == "text/plain" || type == "text/*" || type == "public.text" ||
        type == "public.plain-text" || type == "public.utf8-plain-text") {
      accepts |= DropAcceptsText;
      continue;
    }
    // A file. Anything that is not text arrives here as a path: an image, a
    // PDF, a sound. "public.file-url" is how a Mac names the drop itself, and
    // "image/*" and "public.image" are what an app asking for an image writes.
    if (type == "public.file-url" || type == "public.item" || type == "public.data" ||
        type == "public.content" || type == "public.image" || type == "public.movie" ||
        type == "public.audio" || type == "application/octet-stream" ||
        type.rfind("image/", 0) == 0 || type.rfind("video/", 0) == 0 ||
        type.rfind("audio/", 0) == 0 || type.rfind("application/", 0) == 0) {
      accepts |= DropAcceptsFiles;
      continue;
    }
    // "*/*" is Android's "anything", and the only wildcard that means both.
    if (type == "*/*") {
      accepts |= static_cast<std::uint16_t>(DropAcceptsFiles | DropAcceptsText);
    }
  }
  return accepts;
}

// Whether a view accepting `accepts` would take this payload.
//
// The question the desktop asks before the drop, which is why it is answered
// from what is being dragged rather than from what was dropped: a target that
// said yes and then refused is a cursor that lied.
inline bool wouldAccept(std::uint16_t accepts, const DragPayload &payload) {
  if (accepts == DropAcceptsNone || payload.empty()) {
    return false;
  }
  if ((accepts & DropAcceptsFiles) != 0 && payload.hasFiles()) {
    return true;
  }
  return (accepts & DropAcceptsText) != 0 && payload.hasText();
}

// The nativeID a view would carry to accept these kinds -- the inverse of
// dropAcceptsFrom, here so that a test can round-trip the two and JavaScript
// has one authority to match.
inline std::string dropTargetIdFor(std::uint16_t accepts) {
  std::string id(kDropTargetIdPrefix);
  if ((accepts & DropAcceptsFiles) != 0) {
    id += "files";
  }
  if ((accepts & DropAcceptsText) != 0) {
    if (id.size() > std::string(kDropTargetIdPrefix).size()) {
      id += ",";
    }
    id += "text";
  }
  return id;
}

// --- Dragging out -----------------------------------------------------------
//
// The other direction, and marked the same way -- except that a drag source
// declares *what it is*, not what it takes, so the marker carries a value:
// `basalt-drag:text:hello`, `basalt-drag:files:/tmp/one.txt`.
//
// Declared in advance rather than produced on demand, because every toolkit
// owns the gesture: GtkDragSource, `beginDraggingSessionWithItems:` and
// `DoDragDrop` all start the drag themselves and ask what is being dragged
// synchronously, on the UI thread. An app asked at that moment would have to
// answer from JavaScript on another thread, which is the same problem
// refusing to close has, and it is solved the same way -- say it beforehand.
inline constexpr const char *kDragSourceIdPrefix = "basalt-drag:";

// What a marked view represents, or an empty payload for any other nativeID.
//
// Everything after the second colon is the value, so a path or a line of text
// containing a colon survives: only the first two are structure.
inline DragPayload dragPayloadFrom(const std::string &nativeId) {
  DragPayload payload;
  const std::string prefix(kDragSourceIdPrefix);
  if (nativeId.rfind(prefix, 0) != 0) {
    return payload;
  }
  const size_t kindEnd = nativeId.find(':', prefix.size());
  if (kindEnd == std::string::npos) {
    return payload;
  }
  const std::string kind = nativeId.substr(prefix.size(), kindEnd - prefix.size());
  const std::string value = nativeId.substr(kindEnd + 1);
  if (value.empty()) {
    return payload;
  }
  if (kind == "files") {
    payload.files.push_back(value);
  } else if (kind == "text") {
    payload.text = value;
  }
  return payload;
}

// The inverse, so a test can round-trip it and JavaScript has one authority
// to match. Files win where a payload somehow carries both: a desktop
// dragging a file offers its name as text anyway.
inline std::string dragSourceIdFor(const DragPayload &payload) {
  std::string id(kDragSourceIdPrefix);
  if (payload.hasFiles()) {
    return id + "files:" + payload.files.front();
  }
  if (payload.hasText()) {
    return id + "text:" + payload.text;
  }
  return id;
}

// What happened over a drop target.
//
// One struct and a kind rather than three callbacks, because the host reports
// all three from the same place and JavaScript wants them on one event.
enum class DropPhase {
  // The pointer moved over an accepting view, which is what draws the "you
  // may drop here" state. Sent on entering and while moving within it.
  Over,
  // It left, or the drag ended somewhere else. The app clears that state.
  Leave,
  // It was dropped. The only phase carrying the payload: the desktops do not
  // all reveal what is being dragged until it lands, and one that does should
  // not tempt an app into reading it early.
  Drop,
};

struct DropEvent {
  facebook::react::Tag tag{0};
  DropPhase phase{DropPhase::Over};
  // In the surface root's coordinates, like a touch.
  double x{0.0};
  double y{0.0};
  DragPayload payload;
};

// Set by the module; called by whichever host is running.
void setDropListener(std::function<void(const DropEvent &)> listener);

// The host's half: called on the UI thread.
void reportDrop(const DropEvent &event);

} // namespace basalt
