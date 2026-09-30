// The GTK half of a `<Canvas>`: a raster surface, handed over as a texture.
//
// The third of these, and the shortest, because this host already had the
// seam. AppKit gives the package a CAMetalLayer and lets it present; Win32 has
// neither a layer tree nor a view that is a window, so win32/Win32SkiaPeer.h
// invents `RnWin32Painter` and draws inside the Direct2D walk. Here an RnView
// is a real GtkWidget that already knows how to draw a `GdkTexture` -- it is
// how `<Image>` works -- so a canvas is a view whose texture is replaced every
// frame, and `rn_view_set_texture` is the whole presentation path.
//
// What that buys is the same thing the Win32 peer argues for, and here it
// comes from GTK rather than from us: the canvas is clipped by `overflow:
// hidden`, moved by a ScrollView's offset, faded by `opacity` and turned by
// `transform`, because the widget above it does those things to its own
// children. None of it is code in this file.
//
// ## Raster, and what a GPU path would have to answer
//
// Each frame is drawn by Skia into a CPU surface and copied once into a
// GdkTexture. The archives carry Ganesh over GL -- `skia_use_gl=true` and
// `skia_use_egl=true` -- so the GPU is not missing, it is unclaimed, and the
// reason is in gtk/GtkSkiaContext.h: Ganesh wants a `GrDirectContext` built on
// a current GL context, and GtkGLArea only makes one current inside its own
// `render` signal. Claiming it means inverting who calls whom -- `requestRedraw`
// becoming `gtk_gl_area_queue_render`, and the package's draw running inside
// GTK's callback rather than when the picture arrives. That is a design
// question rather than a port, and it replaces this file without touching
// anything that includes it.
//
// The cost of raster meanwhile is one copy per frame, in the size of the
// canvas rather than the size of the app.

#pragma once

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/mounting/ShadowView.h>

#include <gtk/gtk.h>

namespace basalt {

// Gives `view` a Skia canvas if `shadowView` is a SkiaPictureView, updating its
// size if it already has one. False when the component is something else, so
// the caller can treat this as a filter rather than asking twice.
//
// No repaint callback, where the Win32 one needs one: setting a texture on a
// widget queues its own redraw, because that is what `gtk_widget_queue_draw`
// is for and `rn_view_set_texture` already calls it. Windows repaints on
// demand and had to be told; GTK does not.
//
// Safe to call before `install` has run: without a manager there is nothing to
// register with, so it says so in the log and leaves the view alone rather
// than building a surface no picture could ever reach.
bool applySkiaCanvas(GtkWidget *view, const facebook::react::ShadowView &shadowView);

// Unregisters the canvas for `tag`, if it had one. Called when the mounting
// walk forgets a tag, which is the one moment a view is known to be finished
// with -- and it matters more here than the tag bookkeeping suggests: a canvas
// left registered is a surface the package keeps rendering into, once per
// picture, for a widget nobody will draw again.
void forgetSkiaCanvas(facebook::react::Tag tag);

} // namespace basalt
