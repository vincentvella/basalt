// CSS filter functions, resolved to one colour matrix, one blur and one opacity.
//
// `filter: 'grayscale(1) brightness(1.2)'` on a view reaches both hosts as a
// list of `FilterFunction`s and was dropped by both. Each toolkit has the
// primitives: GSK has a colour-matrix node, a blur node and an opacity node, and
// CALayer takes Core Image filters on macOS. What neither has is the arithmetic,
// which the Filter Effects spec gives exactly -- so it lives here, once, with the
// spec's own numbers.
//
// ## Why one matrix rather than a chain
//
// Seven of the nine functions are affine transformations of colour:
// `brightness`, `contrast`, `grayscale`, `hueRotate`, `invert`, `saturate` and
// `sepia`. Composing two of those is multiplying their matrices, so a list of
// them is one matrix and one node rather than a stack -- and since each is a
// per-pixel affine map, it does not matter whether the blur happens before or
// after: a blur is a weighted average whose weights sum to one, and an affine
// map commutes with that. The same goes for `opacity`, which scales alpha
// linearly.
//
// `dropShadow` is the exception and does not commute with anything: it depends on
// the alpha silhouette at its point in the chain. It is reported as present and
// not applied; see backlog/correctness.md.
//
// Two blurs in one list compose in quadrature -- the sigmas, not the radii --
// because convolving two gaussians gives a gaussian of the combined variance.
//
// Header-only, like core/Gradients.h, and included by the mounting managers,
// which already know React Native.

#pragma once

// For kPi, which is here rather than M_PI for the reason that header gives: MSVC
// defines the POSIX math constants only behind a flag this project does not set.
#include "Gradients.h"

#include <react/renderer/graphics/Filter.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace basalt {

// An affine map of colour: `out = matrix * in + offset`, both in R, G, B, A
// order, with the matrix stored row by row. That is GSK's own shape, and Core
// Image's CIColorMatrix wants the same numbers as four vectors and a bias.
struct ColorMatrix {
  float m[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float offset[4]{0, 0, 0, 0};

  // Row `row`, column `column`.
  float at(int row, int column) const { return m[row * 4 + column]; }

  bool isIdentity() const {
    for (int row = 0; row < 4; row++) {
      for (int column = 0; column < 4; column++) {
        const float wanted = row == column ? 1.0F : 0.0F;
        if (std::fabs(at(row, column) - wanted) > 1e-6F) {
          return false;
        }
      }
      if (std::fabs(offset[row]) > 1e-6F) {
        return false;
      }
    }
    return true;
  }

  // The colour this matrix makes of `in`, which is what the tests assert on and
  // what a host can use to check itself against a rendered pixel.
  void apply(const float in[4], float out[4]) const {
    for (int row = 0; row < 4; row++) {
      out[row] = offset[row];
      for (int column = 0; column < 4; column++) {
        out[row] += at(row, column) * in[column];
      }
    }
  }
};

// `second` after `first`: out = M2 (M1 c + o1) + o2.
inline ColorMatrix composeColorMatrix(const ColorMatrix &first, const ColorMatrix &second) {
  ColorMatrix result;
  for (int row = 0; row < 4; row++) {
    for (int column = 0; column < 4; column++) {
      float sum = 0.0F;
      for (int k = 0; k < 4; k++) {
        sum += second.at(row, k) * first.at(k, column);
      }
      result.m[row * 4 + column] = sum;
    }
    float offset = second.offset[row];
    for (int k = 0; k < 4; k++) {
      offset += second.at(row, k) * first.offset[k];
    }
    result.offset[row] = offset;
  }
  return result;
}

namespace detail {

// The luminance weights every one of these matrices is built from, which are
// Rec. 709's and are what the spec names.
constexpr float kLumaRed = 0.2126F;
constexpr float kLumaGreen = 0.7152F;
constexpr float kLumaBlue = 0.0722F;

inline ColorMatrix scaleRgb(float factor) {
  ColorMatrix matrix;
  matrix.m[0] = factor;
  matrix.m[5] = factor;
  matrix.m[10] = factor;
  return matrix;
}

// The spec's saturate matrix, which `grayscale` is the inverse end of: saturate
// at 0 and grayscale at 1 are the same transformation.
inline ColorMatrix saturate(float amount) {
  // Clamped at zero only. Over-saturating past 1 is allowed and useful.
  const float s = std::max(amount, 0.0F);
  ColorMatrix matrix;
  const float r = kLumaRed;
  const float g = kLumaGreen;
  const float b = kLumaBlue;
  const float row0[4] = {r + (1 - r) * s, g - g * s, b - b * s, 0};
  const float row1[4] = {r - r * s, g + (1 - g) * s, b - b * s, 0};
  const float row2[4] = {r - r * s, g - g * s, b + (1 - b) * s, 0};
  for (int column = 0; column < 4; column++) {
    matrix.m[column] = row0[column];
    matrix.m[4 + column] = row1[column];
    matrix.m[8 + column] = row2[column];
  }
  return matrix;
}

inline ColorMatrix sepia(float amount) {
  const float a = std::clamp(amount, 0.0F, 1.0F);
  // The spec's sepia matrix, interpolated from the identity by `amount`.
  const float full[9] = {0.393F, 0.769F, 0.189F, 0.349F, 0.686F, 0.168F, 0.272F, 0.534F, 0.131F};
  ColorMatrix matrix;
  for (int row = 0; row < 3; row++) {
    for (int column = 0; column < 3; column++) {
      const float identity = row == column ? 1.0F : 0.0F;
      matrix.m[row * 4 + column] = identity + (full[row * 3 + column] - identity) * a;
    }
  }
  return matrix;
}

inline ColorMatrix hueRotate(float degrees) {
  const double radians = static_cast<double>(degrees) * kPi / 180.0;
  const float cosine = static_cast<float>(std::cos(radians));
  const float sine = static_cast<float>(std::sin(radians));
  // The spec's hue-rotate matrix: the luminance-preserving rotation in the plane
  // orthogonal to the grey axis.
  const float rows[9] = {
      0.213F + cosine * 0.787F - sine * 0.213F,
      0.715F - cosine * 0.715F - sine * 0.715F,
      0.072F - cosine * 0.072F + sine * 0.928F,

      0.213F - cosine * 0.213F + sine * 0.143F,
      0.715F + cosine * 0.285F + sine * 0.140F,
      0.072F - cosine * 0.072F - sine * 0.283F,

      0.213F - cosine * 0.213F - sine * 0.787F,
      0.715F - cosine * 0.715F + sine * 0.715F,
      0.072F + cosine * 0.928F + sine * 0.072F,
  };
  ColorMatrix matrix;
  for (int row = 0; row < 3; row++) {
    for (int column = 0; column < 3; column++) {
      matrix.m[row * 4 + column] = rows[row * 3 + column];
    }
  }
  return matrix;
}

} // namespace detail

// What a list of filters comes to, once the arithmetic is done.
struct ResolvedFilters {
  // The seven colour functions, multiplied together. Identity when the list has
  // none of them.
  ColorMatrix matrix;
  bool hasMatrix{false};
  // The CSS blur radius of every `blur()` in the list, composed: sigmas add in
  // quadrature, and the radius is twice the sigma on both hosts.
  float blurRadius{0.0F};
  // Every `opacity()` multiplied together; 1 when the list has none.
  float opacity{1.0F};
  // A `dropShadow()` was asked for and is not applied. Reported so a host can
  // say so once rather than silently dropping it.
  bool hasDropShadow{false};

  bool empty() const { return !hasMatrix && blurRadius <= 0 && opacity >= 1.0F; }
};

inline ResolvedFilters resolveFilters(const std::vector<facebook::react::FilterFunction> &filters) {
  using facebook::react::FilterType;

  ResolvedFilters resolved;
  double variance = 0.0;
  for (const auto &filter : filters) {
    if (filter.type == FilterType::DropShadow) {
      resolved.hasDropShadow = true;
      continue;
    }
    if (!std::holds_alternative<facebook::react::Float>(filter.parameters)) {
      continue;
    }
    const auto amount = static_cast<float>(std::get<facebook::react::Float>(filter.parameters));

    switch (filter.type) {
      case FilterType::Blur: {
        if (amount > 0) {
          // Sigma is half the radius, as it is for an <Image>'s blurRadius and
          // for a box shadow; two blurs compose by their variances.
          const double sigma = static_cast<double>(amount) / 2.0;
          variance += sigma * sigma;
        }
        break;
      }
      case FilterType::Opacity:
        resolved.opacity *= std::clamp(amount, 0.0F, 1.0F);
        break;
      case FilterType::Brightness:
        resolved.matrix = composeColorMatrix(resolved.matrix, detail::scaleRgb(std::max(amount, 0.0F)));
        resolved.hasMatrix = true;
        break;
      case FilterType::Contrast: {
        const float k = std::max(amount, 0.0F);
        ColorMatrix contrast = detail::scaleRgb(k);
        // The intercept that keeps grey in the middle: 0.5 - 0.5k.
        const float intercept = 0.5F - 0.5F * k;
        contrast.offset[0] = intercept;
        contrast.offset[1] = intercept;
        contrast.offset[2] = intercept;
        resolved.matrix = composeColorMatrix(resolved.matrix, contrast);
        resolved.hasMatrix = true;
        break;
      }
      case FilterType::Grayscale:
        // Grayscale is saturate from the other end: 1 is fully grey.
        resolved.matrix = composeColorMatrix(
            resolved.matrix, detail::saturate(1.0F - std::clamp(amount, 0.0F, 1.0F)));
        resolved.hasMatrix = true;
        break;
      case FilterType::Saturate:
        resolved.matrix = composeColorMatrix(resolved.matrix, detail::saturate(amount));
        resolved.hasMatrix = true;
        break;
      case FilterType::HueRotate:
        resolved.matrix = composeColorMatrix(resolved.matrix, detail::hueRotate(amount));
        resolved.hasMatrix = true;
        break;
      case FilterType::Invert: {
        const float a = std::clamp(amount, 0.0F, 1.0F);
        // out = a(1 - c) + (1 - a)c = (1 - 2a)c + a.
        ColorMatrix invert = detail::scaleRgb(1.0F - 2.0F * a);
        invert.offset[0] = a;
        invert.offset[1] = a;
        invert.offset[2] = a;
        resolved.matrix = composeColorMatrix(resolved.matrix, invert);
        resolved.hasMatrix = true;
        break;
      }
      case FilterType::Sepia:
        resolved.matrix = composeColorMatrix(resolved.matrix, detail::sepia(amount));
        resolved.hasMatrix = true;
        break;
      case FilterType::DropShadow:
        break;
    }
  }
  if (variance > 0) {
    resolved.blurRadius = static_cast<float>(std::sqrt(variance) * 2.0);
  }
  return resolved;
}

} // namespace basalt
