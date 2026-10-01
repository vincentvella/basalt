// Drives real ShadowViewMutations through the real Win32MountingManager into
// real views, with no JS runtime, no Hermes and no Metro.
//
// The mirror of gtk/mount_harness_gtk.cpp and appkit/mount_harness_appkit.mm,
// with the same two transactions and the same boxes, which is the point: all
// three platforms share their mutation walk, and the cheapest way to notice
// that sharing has broken is to put the three pictures side by side.
//
// This is the checkpoint between "the mounting manager compiles" and "React
// Native runs". A mistake in the Create/Insert/Update/Remove/Delete handling
// shows up here as a picture rather than as something to be found by review.
//
// With BASALT_SNAPSHOT_DIR set it renders both transactions to PNGs and exits.
// Without it, it opens a window and applies the second transaction after two
// seconds, the way the other two harnesses do.

#include "Win32MountingManager.h"
#include "Win32Snapshot.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

using Microsoft::WRL::ComPtr;
using basalt::Win32MountingManager;
using basalt::win32::RnWin32View;
using facebook::react::ColorComponents;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

ShadowView makeShadowView(Tag tag,
                          float x,
                          float y,
                          float width,
                          float height,
                          float red,
                          float green,
                          float blue,
                          float opacity = 1.0f) {
  auto props = std::make_shared<ViewProps>();
  props->backgroundColor = facebook::react::colorFromComponents(
      ColorComponents{.red = red, .green = green, .blue = blue, .alpha = 1.0f});
  props->opacity = opacity;

  LayoutMetrics layoutMetrics;
  layoutMetrics.frame = {.origin = {.x = x, .y = y},
                         .size = {.width = width, .height = height}};

  ShadowView shadowView;
  shadowView.componentName = "View";
  shadowView.surfaceId = kSurfaceId;
  shadowView.tag = tag;
  shadowView.props = props;
  shadowView.layoutMetrics = layoutMetrics;
  return shadowView;
}

MountingTransaction makeTransaction(MountingTransaction::Number number,
                                    ShadowViewMutationList &&mutations) {
  return MountingTransaction(kSurfaceId, number, std::move(mutations), TransactionTelemetry{});
}

// First transaction: create four views and place them, one of them nested.
ShadowViewMutationList firstTransaction() {
  ShadowViewMutationList mutations;

  const ShadowView blue = makeShadowView(2, 32, 32, 240, 160, 0.30f, 0.55f, 0.95f);
  const ShadowView orange = makeShadowView(3, 296, 32, 240, 160, 0.95f, 0.45f, 0.35f);
  const ShadowView green = makeShadowView(4, 32, 224, 504, 140, 0.35f, 0.80f, 0.55f);
  const ShadowView nested = makeShadowView(5, 24, 24, 120, 90, 1.0f, 1.0f, 1.0f, 0.85f);

  // Fabric emits every Create before the Inserts that place them.
  mutations.push_back(ShadowViewMutation::CreateMutation(blue));
  mutations.push_back(ShadowViewMutation::CreateMutation(orange));
  mutations.push_back(ShadowViewMutation::CreateMutation(green));
  mutations.push_back(ShadowViewMutation::CreateMutation(nested));

  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, blue, 0));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, orange, 1));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, green, 2));
  mutations.push_back(ShadowViewMutation::InsertMutation(blue.tag, nested, 0));

  return mutations;
}

// Second transaction: recolour one view, and remove another entirely. Fabric
// always emits Remove before Delete, and this reproduces that ordering.
ShadowViewMutationList secondTransaction() {
  ShadowViewMutationList mutations;

  // Update: same tag, new props. Recolour blue -> purple and shrink.
  const ShadowView oldBlue = makeShadowView(2, 32, 32, 240, 160, 0.30f, 0.55f, 0.95f);
  const ShadowView newBlue = makeShadowView(2, 32, 32, 240, 100, 0.60f, 0.35f, 0.90f);
  mutations.push_back(ShadowViewMutation::UpdateMutation(oldBlue, newBlue, kSurfaceId));

  // Remove then Delete, the order Fabric guarantees.
  const ShadowView orange = makeShadowView(3, 296, 32, 240, 160, 0.95f, 0.45f, 0.35f);
  mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, orange, 1));
  mutations.push_back(ShadowViewMutation::DeleteMutation(orange));

  return mutations;
}

RnWin32View *makeRoot(Win32MountingManager &manager) {
  // The host owns the root: Fabric never emits a Create for it, because the
  // root shadow node is the base of every diff.
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 640, 420);
  root->setBackgroundColor(0.12f, 0.13f, 0.16f, 1.0f, true);
  return root;
}

int runSnapshots(Win32MountingManager &manager, const std::string &directory) {
  RnWin32View *root = makeRoot(manager);

  // executeMount posts to the UI thread. With no host installed there is no
  // message loop and postToUiThread runs the work inline, so nothing has to be
  // drained here -- unlike the AppKit harness, which spins its run loop. Worth
  // stating rather than looking like an omission.
  manager.executeMount(kSurfaceId, makeTransaction(1, firstTransaction()));
  if (!basalt::win32::writeSnapshot(*root, directory + "/mount-1.png")) {
    std::fputs("could not write mount-1.png\n", stderr);
    return 1;
  }
  std::fputs(root->describeTree().c_str(), stdout);

  manager.executeMount(kSurfaceId, makeTransaction(2, secondTransaction()));
  if (!basalt::win32::writeSnapshot(*root, directory + "/mount-2.png")) {
    std::fputs("could not write mount-2.png\n", stderr);
    return 1;
  }
  std::fputs("---\n", stdout);
  std::fputs(root->describeTree().c_str(), stdout);

  manager.destroySurfaceRoot(kSurfaceId);
  return 0;
}

// --- the windowed mode -------------------------------------------------------

struct HarnessWindow {
  Win32MountingManager *manager = nullptr;
  RnWin32View *root = nullptr;
  ComPtr<ID2D1Factory> factory;
  ComPtr<ID2D1HwndRenderTarget> target;
};

constexpr UINT_PTR kSecondTransactionTimer = 1;

bool ensureTarget(HarnessWindow *harness, HWND hwnd) {
  if (harness->target) {
    return true;
  }
  RECT client{};
  GetClientRect(hwnd, &client);
  const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(client.right - client.left),
                                       static_cast<UINT32>(client.bottom - client.top));
  if (size.width == 0 || size.height == 0) {
    return false;
  }
  return SUCCEEDED(harness->factory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, size),
      &harness->target));
}

LRESULT CALLBACK harnessProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto *harness = reinterpret_cast<HarnessWindow *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  switch (message) {
    case WM_CREATE: {
      auto *create = reinterpret_cast<CREATESTRUCT *>(lparam);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      // Two seconds, then the second transaction -- the same beat the other two
      // harnesses use, so the three can be watched side by side.
      SetTimer(hwnd, kSecondTransactionTimer, 2000, nullptr);
      return 0;
    }

    case WM_TIMER:
      if (wparam == kSecondTransactionTimer && harness != nullptr) {
        KillTimer(hwnd, kSecondTransactionTimer);
        harness->manager->executeMount(kSurfaceId, makeTransaction(2, secondTransaction()));
        std::fputs("---\n", stdout);
        std::fputs(harness->root->describeTree().c_str(), stdout);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_SIZE:
      if (harness != nullptr && harness->target) {
        harness->target->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
      }
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(hwnd, &paint);
      if (harness != nullptr && ensureTarget(harness, hwnd)) {
        harness->target->BeginDraw();
        harness->target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        harness->target->SetTransform(D2D1::Matrix3x2F::Identity());
        harness->root->paint(harness->target.Get());
        if (harness->target->EndDraw() == D2DERR_RECREATE_TARGET) {
          harness->target.Reset();
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

int runWindow(Win32MountingManager &manager) {
  HarnessWindow harness;
  harness.manager = &manager;
  harness.root = makeRoot(manager);
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               harness.factory.GetAddressOf()))) {
    std::fputs("could not create a Direct2D factory\n", stderr);
    return 1;
  }

  const HINSTANCE instance = GetModuleHandle(nullptr);
  WNDCLASSEX windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = harnessProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"BasaltMountHarness";
  if (RegisterClassEx(&windowClass) == 0) {
    std::fputs("could not register the window class\n", stderr);
    return 1;
  }

  RECT wanted{0, 0, 640, 420};
  AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);

  const HWND hwnd = CreateWindowEx(0,
                                   windowClass.lpszClassName,
                                   L"basalt-core \x2014 Windows mount harness",
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   wanted.right - wanted.left,
                                   wanted.bottom - wanted.top,
                                   nullptr,
                                   nullptr,
                                   instance,
                                   &harness);
  if (hwnd == nullptr) {
    std::fputs("could not create the window\n", stderr);
    return 1;
  }

  manager.executeMount(kSurfaceId, makeTransaction(1, firstTransaction()));
  std::fputs(harness.root->describeTree().c_str(), stdout);

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

  // Constructed on this thread: MountingWalk records it and asserts every
  // transaction arrives on the same one.
  Win32MountingManager manager;

  if (const char *directory = std::getenv("BASALT_SNAPSHOT_DIR")) {
    return runSnapshots(manager, directory);
  }
  return runWindow(manager);
}
