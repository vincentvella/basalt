#include "Gestures.h"

#include "PlatformServices.h"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace basalt {

namespace {

// RNGH's ActionType and PointerType live in its JavaScript; only the two values
// this side produces are named here.
constexpr int kPointerTypeMouse = 2;

double number(const folly::dynamic &config, const char *key, double fallback) {
  if (!config.isObject()) {
    return fallback;
  }
  const auto *value = config.get_ptr(key);
  if (value == nullptr || !value->isNumber()) {
    return fallback;
  }
  return value->asDouble();
}

bool hasKey(const folly::dynamic &config, const char *key) {
  return config.isObject() && config.get_ptr(key) != nullptr &&
      !config.get_ptr(key)->isNull();
}

std::vector<int> tagList(const folly::dynamic &config, const char *key) {
  std::vector<int> tags;
  if (!config.isObject()) {
    return tags;
  }
  const auto *value = config.get_ptr(key);
  if (value == nullptr || !value->isArray()) {
    return tags;
  }
  for (const auto &entry : *value) {
    if (entry.isNumber()) {
      tags.push_back(static_cast<int>(entry.asDouble()));
    }
  }
  return tags;
}

bool contains(const std::vector<int> &tags, int tag) {
  return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

} // namespace

struct GestureRegistry::Handler {
  std::string name;
  int tag{0};
  int viewTag{0};
  folly::dynamic config = folly::dynamic::object();

  GestureState state{GestureState::Undetermined};
  bool tracking{false};

  // The attached view's origin in root coordinates, captured when the pointer
  // lands: a gesture payload's `x`/`y` are relative to the view it is attached
  // to, and `absoluteX`/`absoluteY` to the root.
  double originX{0};
  double originY{0};

  double startX{0};
  double startY{0};
  double lastX{0};
  double lastY{0};
  double startTime{0};
  double lastTime{0};
  double velocityX{0};
  double velocityY{0};

  // For multi-tap. Kept between gestures, because the second tap of a double
  // tap is a separate gesture as far as the pointer is concerned.
  int tapCount{0};
  double lastTapTime{0};

  // Bumped on every pointer down, so a timer that fires after the gesture it
  // belonged to can tell that it is stale. Simpler than cancelling timers, and
  // right even when the timer has already begun running.
  int generation{0};
};

double monotonicMilliseconds() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

GestureRegistry &gestures() {
  static GestureRegistry registry;
  return registry;
}

void GestureRegistry::setEmitter(Emitter emitter) {
  emit_ = std::move(emitter);
}

GestureRegistry::Handler *GestureRegistry::find(int handlerTag) {
  for (auto &handler : handlers_) {
    if (handler.tag == handlerTag) {
      return &handler;
    }
  }
  return nullptr;
}

void GestureRegistry::create(const std::string &handlerName,
                             int handlerTag,
                             folly::dynamic config) {
  drop(handlerTag);
  Handler handler;
  handler.name = handlerName;
  handler.tag = handlerTag;
  handler.config = config.isObject() ? std::move(config) : folly::dynamic::object();
  handlers_.push_back(std::move(handler));
}

void GestureRegistry::update(int handlerTag, folly::dynamic config) {
  Handler *handler = find(handlerTag);
  if (handler == nullptr || !config.isObject()) {
    return;
  }
  // Merged rather than replaced: RNGH sends the properties it knows changed,
  // and sends the relations in a second call of their own.
  for (const auto &entry : config.items()) {
    handler->config[entry.first] = entry.second;
  }
}

void GestureRegistry::attach(int handlerTag, int viewTag) {
  if (Handler *handler = find(handlerTag); handler != nullptr) {
    handler->viewTag = viewTag;
  }
}

void GestureRegistry::drop(int handlerTag) {
  handlers_.erase(std::remove_if(handlers_.begin(),
                                 handlers_.end(),
                                 [handlerTag](const Handler &handler) {
                                   return handler.tag == handlerTag;
                                 }),
                  handlers_.end());
  tracking_.erase(std::remove(tracking_.begin(), tracking_.end(), handlerTag), tracking_.end());
}

void GestureRegistry::setStateFromWorklet(int handlerTag, int state) {
  Handler *handler = find(handlerTag);
  if (handler == nullptr || !handler->tracking) {
    return;
  }
  const auto next = static_cast<GestureState>(state);
  if (next == GestureState::Active) {
    // Through the same path a recogniser takes, so that the handlers it
    // competes with are cancelled the way they would be otherwise.
    tryActivate(*handler, handler->lastX, handler->lastY, handler->lastTime);
    return;
  }
  setState(*handler, next, handler->lastX, handler->lastY, handler->lastTime);
}

bool GestureRegistry::empty() const {
  return handlers_.empty();
}

bool GestureRegistry::hasActiveHandler() const {
  for (int tag : tracking_) {
    for (const auto &handler : handlers_) {
      if (handler.tag == tag && handler.state == GestureState::Active) {
        return true;
      }
    }
  }
  return false;
}

folly::dynamic GestureRegistry::payloadFor(const Handler &handler, double x, double y) const {
  folly::dynamic payload = folly::dynamic::object;
  payload["handlerTag"] = handler.tag;
  payload["numberOfPointers"] = 1;
  payload["pointerType"] = kPointerTypeMouse;
  payload["state"] = static_cast<int>(handler.state);
  payload["x"] = x - handler.originX;
  payload["y"] = y - handler.originY;
  payload["absoluteX"] = x;
  payload["absoluteY"] = y;

  if (handler.name == "PanGestureHandler") {
    payload["translationX"] = x - handler.startX;
    payload["translationY"] = y - handler.startY;
    payload["velocityX"] = handler.velocityX;
    payload["velocityY"] = handler.velocityY;
  } else if (handler.name == "LongPressGestureHandler") {
    payload["duration"] = handler.lastTime - handler.startTime;
  }
  return payload;
}

void GestureRegistry::setState(Handler &handler,
                               GestureState next,
                               double x,
                               double y,
                               double timestampMs) {
  if (handler.state == next) {
    return;
  }
  const GestureState previous = handler.state;
  handler.state = next;
  handler.lastTime = timestampMs;

  if (emit_) {
    folly::dynamic payload = payloadFor(handler, x, y);
    payload["oldState"] = static_cast<int>(previous);
    emit_("onGestureHandlerStateChange", std::move(payload));
  }

  // Undetermined is where a handler waits for the next gesture.
  if (next == GestureState::End || next == GestureState::Failed ||
      next == GestureState::Cancelled) {
    handler.tracking = false;
    handler.state = GestureState::Undetermined;
  }
}

void GestureRegistry::emitUpdate(const Handler &handler,
                                 double x,
                                 double y,
                                 double /*timestampMs*/) {
  if (!emit_) {
    return;
  }
  emit_("onGestureHandlerEvent", payloadFor(handler, x, y));
}

void GestureRegistry::cancelOthers(const Handler &activated,
                                   double x,
                                   double y,
                                   double timestampMs) {
  const std::vector<int> simultaneous = tagList(activated.config, "simultaneousHandlers");
  const int activatedTag = activated.tag;

  for (int tag : tracking_) {
    if (tag == activatedTag) {
      continue;
    }
    Handler *other = find(tag);
    if (other == nullptr || !other->tracking) {
      continue;
    }
    if (contains(simultaneous, tag) ||
        contains(tagList(other->config, "simultaneousHandlers"), activatedTag)) {
      continue;
    }
    setState(*other,
             other->state == GestureState::Active ? GestureState::Cancelled
                                                  : GestureState::Failed,
             x,
             y,
             timestampMs);
  }
}

bool GestureRegistry::tryActivate(Handler &handler, double x, double y, double timestampMs) {
  // `waitFor` is RNGH's requireToFail: this handler may not activate while a
  // handler it waits for is still undecided. Checked at the moment of
  // activation rather than tracked as a subscription, which is enough because
  // the next pointer event re-asks.
  for (int tag : tagList(handler.config, "waitFor")) {
    const Handler *other = const_cast<GestureRegistry *>(this)->find(tag);
    if (other != nullptr && other->tracking) {
      return false;
    }
  }

  setState(handler, GestureState::Active, x, y, timestampMs);
  cancelOthers(handler, x, y, timestampMs);
  return true;
}

void GestureRegistry::beginTracking(Handler &handler,
                                    const HitView &view,
                                    double x,
                                    double y,
                                    double timestampMs) {
  handler.tracking = true;
  handler.originX = view.originX;
  handler.originY = view.originY;
  handler.startX = x;
  handler.startY = y;
  handler.lastX = x;
  handler.lastY = y;
  handler.startTime = timestampMs;
  handler.lastTime = timestampMs;
  handler.velocityX = 0;
  handler.velocityY = 0;
  handler.generation = generation_;

  setState(handler, GestureState::Began, x, y, timestampMs);

  if (handler.name == "LongPressGestureHandler") {
    const double minDuration = number(handler.config, "minDurationMs", 500);
    const int generation = handler.generation;
    const int tag = handler.tag;
    postDelayed(minDuration, [this, tag, generation]() {
      Handler *held = find(tag);
      if (held == nullptr || held->generation != generation || !held->tracking ||
          held->state != GestureState::Began) {
        return;
      }
      tryActivate(*held, held->lastX, held->lastY, held->lastTime);
    });
  }

  // A native handler stands for a platform control that handles its own
  // gesture -- RNGH's buttons, and scroll views. Nothing here wraps one, so it
  // activates on contact, which is what those controls do.
  if (handler.name == "NativeViewGestureHandler") {
    tryActivate(handler, x, y, timestampMs);
  }
}

void GestureRegistry::pointerDown(const std::vector<HitView> &chain,
                                  double x,
                                  double y,
                                  double timestampMs) {
  generation_++;
  pointerIsDown_ = true;
  tracking_.clear();

  // Innermost first, which is the order RNGH offers a gesture to handlers in:
  // the closest handler to the touch gets to claim it before its ancestors.
  for (const HitView &view : chain) {
    for (auto &handler : handlers_) {
      if (handler.viewTag != view.tag || handler.viewTag == 0) {
        continue;
      }
      tracking_.push_back(handler.tag);
      beginTracking(handler, view, x, y, timestampMs);
    }
  }
}

void GestureRegistry::pointerMove(double x, double y, double timestampMs) {
  if (!pointerIsDown_) {
    return;
  }

  for (int tag : tracking_) {
    Handler *handler = find(tag);
    if (handler == nullptr || !handler->tracking) {
      continue;
    }

    const double elapsed = timestampMs - handler->lastTime;
    if (elapsed > 0) {
      // Pixels per second, which is what RNGH's velocity is in.
      handler->velocityX = (x - handler->lastX) / elapsed * 1000.0;
      handler->velocityY = (y - handler->lastY) / elapsed * 1000.0;
    }
    handler->lastX = x;
    handler->lastY = y;
    handler->lastTime = timestampMs;

    const double dx = x - handler->startX;
    const double dy = y - handler->startY;
    const double distance = std::sqrt(dx * dx + dy * dy);

    if (handler->state == GestureState::Active) {
      emitUpdate(*handler, x, y, timestampMs);
      continue;
    }
    if (handler->state != GestureState::Began) {
      continue;
    }

    if (handler->name == "PanGestureHandler") {
      // activeOffsetX/Y are thresholds in one direction and take precedence
      // over minDist, which is RNGH's radial default.
      bool activate = false;
      if (hasKey(handler->config, "activeOffsetXStart") ||
          hasKey(handler->config, "activeOffsetXEnd") ||
          hasKey(handler->config, "activeOffsetYStart") ||
          hasKey(handler->config, "activeOffsetYEnd")) {
        activate = dx <= number(handler->config, "activeOffsetXStart", -1e9) ||
            dx >= number(handler->config, "activeOffsetXEnd", 1e9) ||
            dy <= number(handler->config, "activeOffsetYStart", -1e9) ||
            dy >= number(handler->config, "activeOffsetYEnd", 1e9);
      } else {
        activate = distance >= number(handler->config, "minDist", 10);
      }

      const bool fail = dx <= number(handler->config, "failOffsetXStart", -1e9) ||
          dx >= number(handler->config, "failOffsetXEnd", 1e9) ||
          dy <= number(handler->config, "failOffsetYStart", -1e9) ||
          dy >= number(handler->config, "failOffsetYEnd", 1e9);

      if (fail) {
        setState(*handler, GestureState::Failed, x, y, timestampMs);
      } else if (activate && tryActivate(*handler, x, y, timestampMs)) {
        emitUpdate(*handler, x, y, timestampMs);
      }
      continue;
    }

    // A tap or a long press is over as soon as the pointer wanders. maxDist is
    // RNGH's name for the slop in both.
    if (handler->name == "TapGestureHandler" || handler->name == "LongPressGestureHandler") {
      const double slop =
          number(handler->config, "maxDist", handler->name == "TapGestureHandler" ? 25 : 10);
      if (distance > slop) {
        setState(*handler, GestureState::Failed, x, y, timestampMs);
      }
    }
  }
}

void GestureRegistry::pointerUp(double x, double y, double timestampMs) {
  if (!pointerIsDown_) {
    return;
  }
  pointerIsDown_ = false;

  const std::vector<int> tracking = tracking_;
  for (int tag : tracking) {
    Handler *handler = find(tag);
    if (handler == nullptr || !handler->tracking) {
      continue;
    }
    handler->lastTime = timestampMs;

    if (handler->state == GestureState::Active) {
      setState(*handler, GestureState::End, x, y, timestampMs);
      continue;
    }
    if (handler->state != GestureState::Began) {
      continue;
    }

    if (handler->name == "TapGestureHandler") {
      const double maxDuration = number(handler->config, "maxDurationMs", 500);
      const double maxDelay = number(handler->config, "maxDelayMs", 500);
      const int required = static_cast<int>(number(handler->config, "numberOfTaps", 1));

      if (timestampMs - handler->startTime > maxDuration) {
        handler->tapCount = 0;
        setState(*handler, GestureState::Failed, x, y, timestampMs);
        continue;
      }

      handler->tapCount++;
      handler->lastTapTime = timestampMs;

      if (handler->tapCount >= required) {
        handler->tapCount = 0;
        if (tryActivate(*handler, x, y, timestampMs)) {
          setState(*handler, GestureState::End, x, y, timestampMs);
        }
        continue;
      }

      // More taps wanted. The handler stops tracking this pointer but keeps its
      // count, and fails if the next tap does not arrive in time.
      handler->tracking = false;
      handler->state = GestureState::Undetermined;
      const int generation = handler->generation;
      const int handlerTag = handler->tag;
      postDelayed(maxDelay, [this, handlerTag, generation]() {
        Handler *held = find(handlerTag);
        if (held == nullptr || held->generation != generation || held->tapCount == 0) {
          return;
        }
        held->tapCount = 0;
        if (emit_) {
          folly::dynamic payload = payloadFor(*held, held->lastX, held->lastY);
          payload["state"] = static_cast<int>(GestureState::Failed);
          payload["oldState"] = static_cast<int>(GestureState::Began);
          emit_("onGestureHandlerStateChange", std::move(payload));
        }
      });
      continue;
    }

    if (handler->name == "FlingGestureHandler") {
      const double speed =
          std::sqrt(handler->velocityX * handler->velocityX +
                    handler->velocityY * handler->velocityY);
      // RNGH has no configurable threshold for this; its platforms use their
      // own recognisers. 500 px/s is the middle of what each of them treats as
      // a flick.
      if (speed >= 500 && tryActivate(*handler, x, y, timestampMs)) {
        setState(*handler, GestureState::End, x, y, timestampMs);
      } else {
        setState(*handler, GestureState::Failed, x, y, timestampMs);
      }
      continue;
    }

    // Everything still at BEGAN when the pointer lifts did not happen: a long
    // press that was not held, a pan that never moved, a pinch that could never
    // have started.
    setState(*handler, GestureState::Failed, x, y, timestampMs);
  }

  tracking_.clear();
}

void GestureRegistry::pointerCancel() {
  const std::vector<int> tracking = tracking_;
  for (int tag : tracking) {
    Handler *handler = find(tag);
    if (handler == nullptr || !handler->tracking) {
      continue;
    }
    setState(*handler,
             handler->state == GestureState::Active ? GestureState::Cancelled
                                                    : GestureState::Failed,
             handler->lastX,
             handler->lastY,
             handler->lastTime);
  }
  tracking_.clear();
  pointerIsDown_ = false;
}

} // namespace basalt
