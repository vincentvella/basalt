#include "Win32DropTarget.h"

#include "DragAndDrop.h"

#include <objidl.h>
#include <ole2.h>
// HDROP and DragQueryFileW, which are shellapi's and not shlobj's -- shlobj
// includes it on some SDK configurations and not this one, which is the kind
// of difference only a real Windows compile finds.
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace basalt {
namespace {

// The path from the root down to `target`, root first, or empty if the target
// is not in this tree.
//
// This exists because RnWin32View has no parent pointer and the rule needs
// ancestors. The alternative -- a second walk down the tree applying the hit
// rules again -- would be a copy of `hitTest`'s handling of transforms,
// clipping, hidden views and pointer events, which is exactly the kind of
// duplication that drifts. Finding the path costs one traversal of a tree
// that is already small enough to hit-test on every mouse move.
bool pathTo(win32::RnWin32View *view,
            win32::RnWin32View *target,
            std::vector<win32::RnWin32View *> &path) {
  if (view == nullptr) {
    return false;
  }
  path.push_back(view);
  if (view == target) {
    return true;
  }
  for (win32::RnWin32View *child : view->children()) {
    if (pathTo(child, target, path)) {
      return true;
    }
  }
  path.pop_back();
  return false;
}

std::wstring widen(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int needed =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), needed);
  return wide;
}

std::string narrow(const wchar_t *text) {
  if (text == nullptr) {
    return {};
  }
  const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(needed - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
  return out;
}

// What the data object is offering, before the drop.
std::uint16_t offeredBy(IDataObject *data) {
  std::uint16_t offered = DropAcceptsNone;
  if (data == nullptr) {
    return offered;
  }
  FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  if (data->QueryGetData(&files) == S_OK) {
    offered |= DropAcceptsFiles;
  }
  FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  if (data->QueryGetData(&text) == S_OK) {
    offered |= DropAcceptsText;
  }
  return offered;
}

DragPayload payloadFrom(IDataObject *data) {
  DragPayload payload;
  if (data == nullptr) {
    return payload;
  }

  FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM medium{};
  if (data->GetData(&files, &medium) == S_OK) {
    auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    if (drop != nullptr) {
      const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < count; i++) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        if (DragQueryFileW(drop, i, path.data(), length + 1) > 0) {
          payload.files.push_back(narrow(path.c_str()));
        }
      }
      GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
  }

  FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM textMedium{};
  if (data->GetData(&text, &textMedium) == S_OK) {
    auto *characters = static_cast<const wchar_t *>(GlobalLock(textMedium.hGlobal));
    if (characters != nullptr) {
      payload.text = narrow(characters);
      GlobalUnlock(textMedium.hGlobal);
    }
    ReleaseStgMedium(&textMedium);
  }

  return payload;
}

// What OLE asks while a drag this app started is in flight.
class DragSource : public IDropSource {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
    if (out == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *out = static_cast<IDropSource *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = --references_;
    if (left == 0) {
      delete this;
    }
    return left;
  }

  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keys) override {
    if (escapePressed) {
      return DRAGDROP_S_CANCEL;
    }
    // The button coming up is the drop. Left only: this platform starts a
    // drag from a primary press and nothing else.
    if ((keys & MK_LBUTTON) == 0) {
      return DRAGDROP_S_DROP;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*effect*/) override {
    // The system's own cursors, which is what every other application shows.
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }

 private:
  ULONG references_{1};
};

// A data object carrying one thing.
//
// Hand-written because OLE has no simple one: even a single string needs an
// IDataObject, an IEnumFORMATETC to advertise it, and an HGLOBAL to hold it.
// GTK asks for a GValue and AppKit for an id<NSPasteboardWriting>; this is the
// same idea, three interfaces deep.
class SingleFormatData : public IDataObject {
 public:
  SingleFormatData(CLIPFORMAT format, HGLOBAL medium) : format_(format), medium_(medium) {}

  ~SingleFormatData() {
    if (medium_ != nullptr) {
      GlobalFree(medium_);
    }
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
    if (out == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
      *out = static_cast<IDataObject *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = --references_;
    if (left == 0) {
      delete this;
    }
    return left;
  }

  HRESULT STDMETHODCALLTYPE GetData(FORMATETC *request, STGMEDIUM *out) override {
    if (request == nullptr || out == nullptr) {
      return E_POINTER;
    }
    if (request->cfFormat != format_ || (request->tymed & TYMED_HGLOBAL) == 0) {
      return DV_E_FORMATETC;
    }
    // A copy: the receiver owns what it is given, and this object may be
    // asked again.
    const SIZE_T size = GlobalSize(medium_);
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (copy == nullptr) {
      return E_OUTOFMEMORY;
    }
    void *from = GlobalLock(medium_);
    void *to = GlobalLock(copy);
    if (from != nullptr && to != nullptr) {
      memcpy(to, from, size);
    }
    GlobalUnlock(medium_);
    GlobalUnlock(copy);

    out->tymed = TYMED_HGLOBAL;
    out->hGlobal = copy;
    out->pUnkForRelease = nullptr;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *request) override {
    if (request == nullptr) {
      return E_POINTER;
    }
    return (request->cfFormat == format_ && (request->tymed & TYMED_HGLOBAL) != 0)
        ? S_OK
        : DV_E_FORMATETC;
  }

  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override {
    if (out != nullptr) {
      out->ptd = nullptr;
    }
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **out) override {
    if (out == nullptr) {
      return E_POINTER;
    }
    *out = nullptr;
    if (direction != DATADIR_GET) {
      return E_NOTIMPL;
    }
    FORMATETC format{format_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    // SHCreateStdEnumFmtEtc rather than a fourth hand-written interface.
    return SHCreateStdEnumFmtEtc(1, &format, out);
  }

  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

 private:
  ULONG references_{1};
  CLIPFORMAT format_{0};
  HGLOBAL medium_{nullptr};
};

IDataObject *makeTextDataObject(const std::string &text) {
  const std::wstring wide = widen(text);
  const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
  HGLOBAL medium = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (medium == nullptr) {
    return nullptr;
  }
  auto *out = static_cast<wchar_t *>(GlobalLock(medium));
  if (out != nullptr) {
    memcpy(out, wide.c_str(), bytes);
    GlobalUnlock(medium);
  }
  return new SingleFormatData(CF_UNICODETEXT, medium);
}

IDataObject *makeFileDataObject(const std::string &path) {
  const std::wstring wide = widen(path);
  // DROPFILES, then the paths, then a second NUL to end the list. The shell's
  // own format, which is what makes another application receive a file rather
  // than a string that looks like one.
  const SIZE_T bytes = sizeof(DROPFILES) + (wide.size() + 2) * sizeof(wchar_t);
  HGLOBAL medium = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (medium == nullptr) {
    return nullptr;
  }
  auto *drop = static_cast<DROPFILES *>(GlobalLock(medium));
  if (drop != nullptr) {
    ZeroMemory(drop, bytes);
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    auto *names = reinterpret_cast<wchar_t *>(reinterpret_cast<char *>(drop) + sizeof(DROPFILES));
    memcpy(names, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(medium);
  }
  return new SingleFormatData(CF_HDROP, medium);
}

// The OLE drop target for one window.
//
// Reference counted because OLE holds it: `RegisterDragDrop` takes a
// reference and `RevokeDragDrop` drops it, which is why detachDropTarget has
// to run before the window is destroyed.
class DropTarget : public IDropTarget {
 public:
  DropTarget(HWND window, win32::RnWin32View *root) : window_(window), root_(root) {}

  // --- IUnknown ---
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
    if (out == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *out = static_cast<IDropTarget *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = --references_;
    if (left == 0) {
      delete this;
    }
    return left;
  }

  // --- IDropTarget ---
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data,
                                      DWORD /*keys*/,
                                      POINTL point,
                                      DWORD *effect) override {
    offered_ = offeredBy(data);
    data_ = data;
    return update(point, effect);
  }

  HRESULT STDMETHODCALLTYPE DragOver(DWORD /*keys*/, POINTL point, DWORD *effect) override {
    return update(point, effect);
  }

  HRESULT STDMETHODCALLTYPE DragLeave() override {
    reportLeave(0.0, 0.0);
    offered_ = DropAcceptsNone;
    data_ = nullptr;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Drop(IDataObject *data,
                                 DWORD /*keys*/,
                                 POINTL point,
                                 DWORD *effect) override {
    const DragPayload payload = payloadFrom(data);
    // Asked again from the contents, for the reason the other two hosts give:
    // this is the moment they are known.
    std::uint16_t carries = DropAcceptsNone;
    if (payload.hasFiles()) {
      carries |= DropAcceptsFiles;
    }
    if (payload.hasText()) {
      carries |= DropAcceptsText;
    }

    const POINT local = toClient(point);
    const facebook::react::Tag found =
        dropTargetAt(root_, local.x, local.y, carries);
    current_ = 0;
    data_ = nullptr;
    if (found == 0) {
      if (effect != nullptr) {
        *effect = DROPEFFECT_NONE;
      }
      return S_OK;
    }

    DropEvent event;
    event.tag = found;
    event.phase = DropPhase::Drop;
    event.x = local.x;
    event.y = local.y;
    event.payload = payload;
    reportDrop(event);
    if (effect != nullptr) {
      *effect = DROPEFFECT_COPY;
    }
    return S_OK;
  }

 private:
  POINT toClient(POINTL point) const {
    POINT local{point.x, point.y};
    ScreenToClient(window_, &local);
    return local;
  }

  void reportLeave(double x, double y) {
    if (current_ == 0) {
      return;
    }
    DropEvent event;
    event.tag = current_;
    event.phase = DropPhase::Leave;
    event.x = x;
    event.y = y;
    basalt::reportDrop(event);
    current_ = 0;
  }

  HRESULT update(POINTL point, DWORD *effect) {
    const POINT local = toClient(point);
    const facebook::react::Tag found = dropTargetAt(root_, local.x, local.y, offered_);

    if (found != current_) {
      reportLeave(local.x, local.y);
    }
    if (found == 0) {
      if (effect != nullptr) {
        *effect = DROPEFFECT_NONE;
      }
      return S_OK;
    }
    current_ = found;

    DropEvent event;
    event.tag = found;
    event.phase = DropPhase::Over;
    event.x = local.x;
    event.y = local.y;
    basalt::reportDrop(event);
    if (effect != nullptr) {
      *effect = DROPEFFECT_COPY;
    }
    return S_OK;
  }

  ULONG references_{1};
  HWND window_{nullptr};
  win32::RnWin32View *root_{nullptr};
  IDataObject *data_{nullptr};
  std::uint16_t offered_{DropAcceptsNone};
  facebook::react::Tag current_{0};
};

} // namespace

facebook::react::Tag dropTargetAt(win32::RnWin32View *root,
                                  double x,
                                  double y,
                                  std::uint16_t accepts) {
  if (root == nullptr) {
    return 0;
  }
  win32::RnWin32View *hit =
      win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));
  if (hit == nullptr) {
    return 0;
  }

  std::vector<win32::RnWin32View *> path;
  if (!pathTo(root, hit, path)) {
    return 0;
  }
  // Deepest first, which is the rule: a drop target inside another wins.
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    const std::uint16_t wanted = dropAcceptsFrom((*it)->nativeId());
    if (wanted != DropAcceptsNone && (wanted & accepts) != 0) {
      return static_cast<facebook::react::Tag>((*it)->tag());
    }
  }
  return 0;
}

DragPayload dragPayloadAt(win32::RnWin32View *root, double x, double y) {
  if (root == nullptr) {
    return {};
  }
  win32::RnWin32View *hit =
      win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));
  if (hit == nullptr) {
    return {};
  }
  std::vector<win32::RnWin32View *> path;
  if (!pathTo(root, hit, path)) {
    return {};
  }
  // Deepest first, the same rule the drop side applies.
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    const DragPayload payload = dragPayloadFrom((*it)->nativeId());
    if (!payload.empty()) {
      return payload;
    }
  }
  return {};
}

bool beginDragIfMarked(win32::RnWin32View *root, double x, double y) {
  const DragPayload payload = dragPayloadAt(root, x, y);
  if (payload.empty()) {
    return false;
  }

  IDataObject *data = nullptr;
  if (payload.hasFiles()) {
    data = makeFileDataObject(payload.files.front());
  } else {
    data = makeTextDataObject(payload.text);
  }
  if (data == nullptr) {
    return false;
  }

  auto *source = new DragSource();
  DWORD effect = DROPEFFECT_NONE;
  // Modal: this does not return until the drop or the cancel. Which is why
  // the caller has to treat the gesture as gone -- everything that would have
  // continued the press happens inside here, to OLE.
  DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
  source->Release();
  data->Release();
  return true;
}

void attachDropTarget(HWND window, win32::RnWin32View *root) {
  if (window == nullptr || root == nullptr) {
    return;
  }
  auto *target = new DropTarget(window, root);
  // OLE takes its own reference; this one is ours to drop either way.
  RegisterDragDrop(window, target);
  target->Release();
}

void detachDropTarget(HWND window) {
  if (window != nullptr) {
    RevokeDragDrop(window);
  }
}

} // namespace basalt
