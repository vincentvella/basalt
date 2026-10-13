// Where a child-window peer is allowed to paint.
//
// A `<TextInput>` on this host is a real EDIT control, so it is a child window
// rather than something Direct2D draws. That buys the IME, the clipboard and
// every binding a Windows user expects, and it costs the one thing every other
// view gets for free: a place in the painting order. A child window is drawn by
// USER32 after the parent's client area, so it sits above everything the React
// tree drew, whatever the tree says. Two things follow, and a person sees both:
//
//   - A field in a `<ScrollView>` does not clip to it. Scroll the list until
//     the field is half out and the half that should be gone goes on being
//     drawn over the list's neighbour.
//   - A field cannot be covered. Open a modal over a pane that has one and the
//     field floats on top of the modal, at full brightness, still editable.
//
// Both are the same missing fact -- which pixels of the field are still the
// field's -- and Windows has somewhere to put it: `SetWindowRgn`. A window with
// a region is displayed only inside it, and clicks outside it fall through to
// the parent. So the job is to work the region out from the view tree, which is
// what this file is: pure geometry over `RnWin32View`, with no window, no
// control and no React Native in it, so that it can be tested with none of
// them.
//
// ## What counts as covering it
//
// Clipping is exact: a clipping ancestor's box is a rectangle and the
// intersection of rectangles is the answer.
//
// Occlusion is not, and cannot be. The right behaviour for a field under
// kino's modal scrim -- `#000000A0` over the whole window -- is for the field
// to be *dimmed*, and a child window cannot be dimmed by something painted into
// its parent: the two are composited by USER32, in that order, with no blend to
// take part in. The choice is therefore between showing the field undimmed and
// hiding it, and hiding it is the better wrong answer: an undimmed field over a
// modal is the bug people report, while a field that goes quiet under a scrim
// reads as part of the modal.
//
// So anything painted after the field that is not fully transparent counts as
// covering it. That is deliberately more eager than "opaque": a 63%-black
// backdrop blends to something no child window can match, so there is nothing
// to be gained by letting it show through at 100%.
//
// What does *not* count: a view with no background at all, which is every
// layout wrapper in a real tree and the reason this rule produces no false
// positives in practice, and a transformed view, whose box is not a rectangle
// in these coordinates and which is therefore left alone rather than guessed
// at. A `<Text>` drawn over a field is also left alone -- glyphs are not a box.
//
// ## Transforms
//
// Ignored throughout, as `Win32TextInputManager` already ignores them when it
// places the peer: a window cannot be rotated, so a transformed field's peer
// sits where the untransformed one would, and clipping it to a rotated
// ancestor's bounding box would only move the error around.

#pragma once

#include <windows.h>

#include <vector>

namespace basalt::win32 {

class RnWin32View;

// Where `view`'s peer may paint, all in the surface root's coordinates.
struct PeerClip {
  // False when the view is not on screen at all: hidden, detached, or scrolled
  // entirely out of a clipping ancestor. Nothing else in the struct means
  // anything then.
  bool visible{false};

  // The view's own box, unclipped. This is where the control goes -- a control
  // narrowed to its visible part would lay its text out in the wrong width.
  RECT bounds{};

  // `bounds` narrowed by every clipping ancestor. Never empty when `visible`.
  RECT clip{};

  // Boxes painted after the view that cover part of it, already narrowed to
  // their own clipping ancestors. Each is taken out of the region.
  std::vector<RECT> occluders{};
};

// Walks the tree from `root` once, in painting order, and answers for `view`.
//
// `root` is the surface root, whose coordinates are the window's client
// coordinates on this host. A view that is not under `root` comes back
// invisible, which is what a peer between a Remove and its Delete looks like.
PeerClip peerClip(RnWin32View *view, RnWin32View *root);

} // namespace basalt::win32
