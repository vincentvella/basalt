// The Windows view layer, driven by hand-written frames.
//
// The same standing-in-for-Fabric exercise as gtk/demo_layout_gtk.cpp and
// appkit/demo_layout_appkit.mm, with the same boxes at the same coordinates, so
// the three platforms can be compared directly rather than by eye. Builds and
// runs without React Native's C++ core, which keeps the paint and placement
// path verifiable on its own.
//
// Three modes. BASALT_DUMP_TREE prints the tree and exits. BASALT_SNAPSHOT=<png>
// renders the hierarchy offscreen and writes it, which is the mode that proves
// anything: the tree dump would look identical whether the transform
// composition works or not, because it prints the frames that were set rather
// than where they landed. Neither needs a window. Without either, it opens one.
//
// A console subsystem rather than a GUI one, deliberately: BASALT_DUMP_TREE has
// to reach a pipe, and `scripts/compare_hosts.sh` is what reads it.

#include "RnWin32View.h"
#include "Win32Snapshot.h"

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using basalt::win32::RnWin32View;

namespace {

// The demo owns its views. In the real thing the mounting registry does; a view
// never owns its children, which is what makes Fabric's detach-without-destroy
// structural rather than a reference count to get right.
std::vector<std::unique_ptr<RnWin32View>> g_views;

RnWin32View *makeBox(int32_t tag,
                     float x,
                     float y,
                     float w,
                     float h,
                     float r,
                     float g,
                     float b,
                     float a) {
  auto view = std::make_unique<RnWin32View>(tag);
  view->setFrame(x, y, w, h);
  view->setBackgroundColor(r, g, b, a, true);
  RnWin32View *raw = view.get();
  g_views.push_back(std::move(view));
  return raw;
}

RnWin32View *buildTree() {
  RnWin32View *root = makeBox(1, 0, 0, 640, 420, 0.12f, 0.13f, 0.16f, 1.0f);

  // A row of children, absolutely placed the way Yoga would have resolved them.
  RnWin32View *a = makeBox(2, 32, 32, 240, 160, 0.30f, 0.55f, 0.95f, 1.0f);
  RnWin32View *b = makeBox(3, 296, 32, 240, 160, 0.95f, 0.45f, 0.35f, 1.0f);
  RnWin32View *c = makeBox(4, 32, 224, 504, 140, 0.35f, 0.80f, 0.55f, 1.0f);

  root->insertChild(a, 0);
  root->insertChild(b, 1);
  root->insertChild(c, 2);

  // Nested child, to prove coordinates are parent-relative. Win32 and React
  // Native agree on a top-left origin, so unlike the AppKit side there is no
  // flip for this to catch -- what it catches here is a transform composed in
  // the wrong order, which puts it 24 units from the wrong edge just as surely.
  RnWin32View *nested = makeBox(5, 24, 24, 120, 90, 1.0f, 1.0f, 1.0f, 0.85f);
  a->insertChild(nested, 0);

  // Half-opacity child, to prove the opacity layer. Distinct from the alpha
  // above: that one is a translucent colour, this one is a translucent view.
  RnWin32View *faded = makeBox(6, 24, 24, 160, 90, 0.1f, 0.1f, 0.1f, 1.0f);
  faded->setOpacity(0.4f);
  c->insertChild(faded, 0);

  return root;
}

// --- The window ------------------------------------------------------------
//
// One HWND for the whole tree. See RnWin32View.h for why a view is not a
// window; this is the other half of that decision, and it is what the host will
// grow into.

struct DemoWindow {
  RnWin32View *root = nullptr;
  ComPtr<ID2D1Factory> factory;
  ComPtr<ID2D1HwndRenderTarget> target;
};

bool ensureTarget(DemoWindow *demo, HWND hwnd) {
  if (demo->target) {
    return true;
  }
  RECT client{};
  GetClientRect(hwnd, &client);
  const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(client.right - client.left),
                                       static_cast<UINT32>(client.bottom - client.top));
  if (size.width == 0 || size.height == 0) {
    return false;
  }
  return SUCCEEDED(demo->factory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, size), &demo->target));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto *demo = reinterpret_cast<DemoWindow *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  switch (message) {
    case WM_CREATE: {
      auto *create = reinterpret_cast<CREATESTRUCT *>(lparam);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      return 0;
    }

    case WM_SIZE:
      if (demo != nullptr && demo->target) {
        demo->target->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
      }
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(hwnd, &paint);
      if (demo != nullptr && ensureTarget(demo, hwnd)) {
        demo->target->BeginDraw();
        demo->target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        demo->target->SetTransform(D2D1::Matrix3x2F::Identity());
        demo->root->paint(demo->target.Get());
        // A lost device is reported here and nowhere else. Dropping the target
        // is the whole recovery: the next WM_PAINT rebuilds it.
        if (demo->target->EndDraw() == D2DERR_RECREATE_TARGET) {
          demo->target.Reset();
        }
      }
      EndPaint(hwnd, &paint);
      return 0;
    }

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}

int runWindow(RnWin32View *root) {
  DemoWindow demo;
  demo.root = root;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, demo.factory.GetAddressOf()))) {
    std::fputs("could not create a Direct2D factory\n", stderr);
    return 1;
  }

  const HINSTANCE instance = GetModuleHandle(nullptr);
  WNDCLASSEX windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = windowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"BasaltDemoLayout";
  if (RegisterClassEx(&windowClass) == 0) {
    std::fputs("could not register the window class\n", stderr);
    return 1;
  }

  // The client area is the surface, so the window is grown by whatever the
  // frame costs. Getting this wrong scales the whole tree by a few per cent,
  // which is exactly the kind of difference a cross-host comparison is for.
  RECT wanted{0, 0, static_cast<LONG>(root->frame().width),
              static_cast<LONG>(root->frame().height)};
  AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);

  const HWND hwnd = CreateWindowEx(0,
                                   windowClass.lpszClassName,
                                   L"basalt-core \x2014 Windows layout skeleton",
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   wanted.right - wanted.left,
                                   wanted.bottom - wanted.top,
                                   nullptr,
                                   nullptr,
                                   instance,
                                   &demo);
  if (hwnd == nullptr) {
    std::fputs("could not create the window\n", stderr);
    return 1;
  }

  ShowWindow(hwnd, SW_SHOWNORMAL);
  UpdateWindow(hwnd);

  MSG message{};
  while (GetMessage(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }
  return static_cast<int>(message.wParam);
}

} // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  RnWin32View *root = buildTree();

  if (std::getenv("BASALT_DUMP_TREE") != nullptr) {
    std::fputs(root->describeTree().c_str(), stdout);
    return 0;
  }

  if (const char *snapshotPath = std::getenv("BASALT_SNAPSHOT")) {
    return basalt::win32::writeSnapshot(*root, snapshotPath) ? 0 : 1;
  }

  return runWindow(root);
}
