#include "DesktopControls.h"

#include <react/renderer/components/FBReactNativeSpec/ComponentDescriptors.h>
#include <react/renderer/components/FBReactNativeSpec/EventEmitters.h>
#include <react/renderer/components/FBReactNativeSpec/Props.h>
#include <react/renderer/components/modal/ModalHostViewShadowNode.h>
#include <react/renderer/components/switch/AppleSwitchShadowNode.h>
#include <react/renderer/graphics/ColorComponents.h>

#include <cstring>

namespace facebook::react {

// React Native's only <Switch> shadow node is the Apple one, and the two
// translation units that finish it are both Objective-C. This is the third,
// and the constants are in DesktopControls.h beside everything else a switch
// is made of.
//
// The component name is not "AppleSwitch" despite the file it comes from:
// React Native's JavaScript renders `Switch`, and a mismatch here is the kind
// that renders a blank box rather than failing.
extern const char AppleSwitchComponentName[] = "Switch";

Size SwitchShadowNode::measureContent(const LayoutContext & /*layoutContext*/,
                                      const LayoutConstraints & /*layoutConstraints*/) const {
  return {.width = basalt::kSwitchWidth, .height = basalt::kSwitchHeight};
}

} // namespace facebook::react

namespace basalt {

using facebook::react::ActivityIndicatorViewProps;
using facebook::react::ActivityIndicatorViewSize;
using facebook::react::ModalHostViewShadowNode;
using facebook::react::PullToRefreshViewProps;
using facebook::react::ShadowView;
using facebook::react::SharedColor;
using facebook::react::Size;
using facebook::react::SwitchProps;

namespace {

// A React Native colour into the four floats every view layer here wants.
// Returns false for an unset colour, which is not the same as transparent: a
// control with no colour draws in the platform's own, which is the behaviour
// React Native documents for all three of these props.
bool readColor(const SharedColor &color, float out[4]) {
  if (!color) {
    return false;
  }
  const auto components = colorComponentsFromColor(color);
  out[0] = components.red;
  out[1] = components.green;
  out[2] = components.blue;
  out[3] = components.alpha;
  return true;
}

bool sameColor(const float a[4], const float b[4]) {
  return std::memcmp(a, b, sizeof(float) * 4) == 0;
}

} // namespace

ControlKind controlKindFor(const char *componentName) {
  if (componentName == nullptr) {
    return ControlKind::None;
  }
  if (std::strcmp(componentName, "ActivityIndicatorView") == 0) {
    return ControlKind::ActivityIndicator;
  }
  if (std::strcmp(componentName, "Switch") == 0) {
    return ControlKind::Switch;
  }
  if (std::strcmp(componentName, "PullToRefreshView") == 0) {
    return ControlKind::PullToRefresh;
  }
  return ControlKind::None;
}

bool ControlState::operator==(const ControlState &other) const {
  return kind == other.kind && on == other.on && disabled == other.disabled && large == other.large &&
      hidesWhenStopped == other.hidesWhenStopped && hasForeground == other.hasForeground &&
      sameColor(foreground, other.foreground) && hasTrackOn == other.hasTrackOn &&
      sameColor(trackOn, other.trackOn) && hasTrackOff == other.hasTrackOff &&
      sameColor(trackOff, other.trackOff);
}

ControlState controlStateOf(const ShadowView &shadowView) {
  ControlState state;
  state.kind = controlKindFor(shadowView.componentName);

  switch (state.kind) {
    case ControlKind::ActivityIndicator: {
      const auto props = std::dynamic_pointer_cast<const ActivityIndicatorViewProps>(shadowView.props);
      if (props == nullptr) {
        break;
      }
      state.on = props->animating;
      state.large = props->size == ActivityIndicatorViewSize::Large;
      state.hidesWhenStopped = props->hidesWhenStopped;
      state.hasForeground = readColor(props->color, state.foreground);
      break;
    }
    case ControlKind::Switch: {
      const auto props = std::dynamic_pointer_cast<const SwitchProps>(shadowView.props);
      if (props == nullptr) {
        break;
      }
      state.on = props->value;
      state.disabled = props->disabled;
      // `thumbColor` is the modern prop and `thumbTintColor` the deprecated
      // one React Native still parses. Newer first, so an app that sets both
      // gets the one it means.
      state.hasForeground = readColor(props->thumbColor, state.foreground) ||
          readColor(props->thumbTintColor, state.foreground);
      // Likewise: `trackColor={{true, false}}` parses into these two, and the
      // old flat `onTintColor` / `tintColor` are the fallbacks.
      state.hasTrackOn =
          readColor(props->trackColorForTrue, state.trackOn) || readColor(props->onTintColor, state.trackOn);
      state.hasTrackOff =
          readColor(props->trackColorForFalse, state.trackOff) || readColor(props->tintColor, state.trackOff);
      break;
    }
    case ControlKind::PullToRefresh: {
      const auto props = std::dynamic_pointer_cast<const PullToRefreshViewProps>(shadowView.props);
      if (props == nullptr) {
        break;
      }
      state.on = props->refreshing;
      state.hasForeground = readColor(props->tintColor, state.foreground);
      // A refresh control that is not refreshing shows nothing, which is the
      // same rule an indicator states as `hidesWhenStopped`.
      state.hidesWhenStopped = true;
      break;
    }
    case ControlKind::None:
      break;
  }

  return state;
}

std::string describeControl(const ControlState &state) {
  switch (state.kind) {
    case ControlKind::None:
      return {};
    case ControlKind::ActivityIndicator: {
      std::string out = state.large ? "spinner-large" : "spinner";
      out += state.on ? ":animating" : ":stopped";
      return out;
    }
    case ControlKind::Switch: {
      std::string out = state.on ? "switch:on" : "switch:off";
      if (state.disabled) {
        out += ":disabled";
      }
      return out;
    }
    case ControlKind::PullToRefresh:
      return state.on ? "refresh:refreshing" : "refresh:idle";
  }
  return {};
}

void emitSwitchChange(const facebook::react::EventEmitter::Shared &emitter, facebook::react::Tag tag, bool value) {
  const auto typed = std::dynamic_pointer_cast<const facebook::react::SwitchEventEmitter>(emitter);
  if (typed == nullptr) {
    return;
  }
  typed->onChange({.value = value, .target = static_cast<int>(tag)});
}

void emitRefresh(const facebook::react::EventEmitter::Shared &emitter) {
  const auto typed = std::dynamic_pointer_cast<const facebook::react::PullToRefreshViewEventEmitter>(emitter);
  if (typed != nullptr) {
    typed->onRefresh({});
  }
}

void emitModalShow(const facebook::react::EventEmitter::Shared &emitter) {
  const auto typed = std::dynamic_pointer_cast<const facebook::react::ModalHostViewEventEmitter>(emitter);
  if (typed != nullptr) {
    typed->onShow({});
  }
}

void emitModalRequestClose(const facebook::react::EventEmitter::Shared &emitter) {
  const auto typed = std::dynamic_pointer_cast<const facebook::react::ModalHostViewEventEmitter>(emitter);
  if (typed != nullptr) {
    typed->onRequestClose({});
  }
}

namespace {

// The modal's state, or null for anything that is not a modal -- including a
// modal whose state has not been created yet, which a Delete mutation's
// ShadowView looks like.
std::shared_ptr<const ModalHostViewShadowNode::ConcreteState> modalStateOf(const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr || std::strcmp(shadowView.componentName, "ModalHostView") != 0) {
    return nullptr;
  }
  return std::static_pointer_cast<const ModalHostViewShadowNode::ConcreteState>(shadowView.state);
}

} // namespace

bool modalScreenSizeNeedsUpdate(const ShadowView &shadowView, float width, float height) {
  const auto state = modalStateOf(shadowView);
  if (state == nullptr) {
    return false;
  }
  if (width <= 0.0F || height <= 0.0F) {
    // Before the window has a size. Committing zero would be committing the
    // very value that makes a modal invisible.
    return false;
  }
  const Size &screen = state->getData().screenSize;
  return screen.width != width || screen.height != height;
}

void updateModalScreenSize(const ShadowView &shadowView, float width, float height) {
  const auto state = modalStateOf(shadowView);
  if (state == nullptr) {
    return;
  }
  state->updateState(facebook::react::ModalHostViewState{Size{.width = width, .height = height}});
}

} // namespace basalt
