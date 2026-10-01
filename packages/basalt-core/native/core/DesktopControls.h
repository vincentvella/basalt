// The four React Native components that are a control rather than a box.
//
// <ActivityIndicator>, <Switch>, <RefreshControl> and <Modal> have almost
// nothing in common on screen and everything in common here: each one is a
// handful of props that three view layers have to read the same way, and each
// one would otherwise be read three times, slightly differently. Reading them
// once is what keeps a switch the same size and the same colour on GTK, AppKit
// and Win32 -- which `scripts/compare_hosts.sh` checks by diffing the tree
// dumps line by line, and which a per-host `dynamic_pointer_cast` would have
// quietly broken the first time one host defaulted a colour.
//
// What is deliberately *not* here is any drawing. A GtkSwitch, an NSSwitch and
// a Direct2D rounded rectangle have no shared vocabulary, so the split is the
// same one the rest of this project makes: the props are portable, the pixels
// are not.
//
// Two shadow-node obligations also live here, because React Native leaves them
// to the platform and both platform answers it ships are Objective-C:
//
//   * `SwitchShadowNode::measureContent`. React Native's only <Switch> shadow
//     node is the Apple one, and it measures by instantiating a UISwitch or an
//     NSSwitch on the main thread. The size below is a constant instead, which
//     is also why a switch is the same size on all three desktops rather than
//     whatever each toolkit's theme happens to want.
//
//   * The <Modal>'s screen size. `ModalHostViewScreenSize()` is already defined
//     by React Native's own cxx platform file and returns zero, so a modal
//     would lay out 0x0 and render nothing. The host sends the real size
//     through the shadow node's state instead, exactly as the iOS host does;
//     see `modalScreenSizeNeedsUpdate` below.

#pragma once

#include "ControlMetrics.h"

#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <string>

namespace basalt {

// --- What kind of control a component name means ----------------------------

enum class ControlKind {
  None,
  // <ActivityIndicator>. Spins while `animating`.
  ActivityIndicator,
  // <Switch>. On or off, and possibly disabled.
  Switch,
  // <RefreshControl>. A spinner that is on while `refreshing`, plus the pull
  // that turns it on; see PullToRefresh.h for the pull.
  PullToRefresh,
};

// The component name React Native mounts each of these under. Free functions
// rather than a table so a caller can compare without allocating.
ControlKind controlKindFor(const char *componentName);

// --- The props, read once ----------------------------------------------------

// Every control's state, flattened. One struct rather than three because the
// three overlap almost completely and every host stores exactly one of these
// per view; three would be three unions or three fields.
struct ControlState {
  ControlKind kind = ControlKind::None;

  // A switch's `value`; an indicator's `animating`; a refresh control's
  // `refreshing`. The one bit every control has.
  bool on = false;

  // <Switch> only. A disabled switch is drawn dimmed and does not toggle.
  bool disabled = false;

  // <ActivityIndicator> only: `size="large"`.
  bool large = false;

  // <ActivityIndicator> only. When set -- which is React Native's default --
  // a stopped indicator is not drawn at all rather than drawn still.
  bool hidesWhenStopped = true;

  // The colour of the moving part: an indicator's `color`, a refresh control's
  // `tintColor`, a switch's `thumbColor`. Unset means the platform's own.
  bool hasForeground = false;
  float foreground[4] = {0.0F, 0.0F, 0.0F, 0.0F};

  // <Switch> only: `trackColor.true` and `trackColor.false`.
  bool hasTrackOn = false;
  float trackOn[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  bool hasTrackOff = false;
  float trackOff[4] = {0.0F, 0.0F, 0.0F, 0.0F};

  bool operator==(const ControlState &other) const;
  bool operator!=(const ControlState &other) const { return !(*this == other); }
};

// The control state a mounted view carries, or a `None` state for every
// component that is not one of the three. Safe to call on any ShadowView.
ControlState controlStateOf(const facebook::react::ShadowView &shadowView);

// The field all three hosts print in `describeTree`, or an empty string for a
// view that is not a control.
//
// Written once because the tree dump is a cross-platform contract: three hosts
// formatting the same state their own way is a diff on every line, which is a
// mistake this project has already made twice (phase 53's `role=`, and
// `focusable` printing in two places on GTK).
std::string describeControl(const ControlState &state);

// --- Metrics -----------------------------------------------------------------
//
// kSwitchWidth, kSwitchHeight, kSpinnerSmall and kSpinnerLarge are in
// ControlMetrics.h next door, because the Windows view layer draws a switch and
// a spinner and cannot include this file; see its header.

// How far a scroll view has to be pulled past its top before a <RefreshControl>
// fires. React Native's iOS control uses roughly this.
inline constexpr double kPullToRefreshThreshold = 80.0;

// How tall a <RefreshControl> is while it is refreshing. The JavaScript side
// puts this in the view's style, so Yoga makes room for it; the number is here
// so the two cannot disagree.
inline constexpr float kRefreshControlHeight = 40.0F;

// --- The events ---------------------------------------------------------------
//
// Four one-line calls that exist as functions so that the codegen event
// emitters stay out of MountingWalk.h and out of three view layers. Each is a
// no-op for a null emitter, which is what a view between a Remove and its
// Delete has.

// <Switch> was toggled. `tag` is the switch's own, which React Native's payload
// carries as `target`.
void emitSwitchChange(const facebook::react::EventEmitter::Shared &emitter, facebook::react::Tag tag, bool value);

// <RefreshControl> was pulled far enough. React Native expects this once per
// pull; core/PullToRefresh.h is what makes that true.
void emitRefresh(const facebook::react::EventEmitter::Shared &emitter);

// <Modal> appeared. React Native's contract is that this fires after the modal
// is on screen, which here is after the transaction that mounted it.
void emitModalShow(const facebook::react::EventEmitter::Shared &emitter);

// The person asked for the <Modal> to close -- Escape, on a desktop, which is
// the same role Android's back button plays. React Native does not close the
// modal itself: the app's handler sets `visible` to false, and not handling it
// leaves the modal up, which is deliberate.
void emitModalRequestClose(const facebook::react::EventEmitter::Shared &emitter);

// --- <Modal> -----------------------------------------------------------------

// Whether this view is a <Modal> whose state still says the screen is a
// different size than it is, and so needs the host to commit a new one.
//
// `ModalHostViewComponentDescriptor::adopt` reads the size out of the shadow
// node's state and applies it as the node's own, which is how a modal comes to
// cover the window without the app giving it a size. React Native's cxx
// platform reports that size as zero -- so without this a modal lays out 0x0
// and the app shows nothing at all, with no error anywhere.
//
// Returns false for every component that is not a <Modal>, and for a modal
// already holding this size: committing state unconditionally would commit on
// every mount forever.
bool modalScreenSizeNeedsUpdate(const facebook::react::ShadowView &shadowView, float width, float height);

// Commits `width` x `height` as the modal's screen size. Call only when
// `modalScreenSizeNeedsUpdate` said so.
void updateModalScreenSize(const facebook::react::ShadowView &shadowView, float width, float height);

} // namespace basalt
