#include "platform/trash.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shlobj.h>
#include <shobjidl.h>

namespace platform::trash {
namespace {

namespace fs = std::filesystem;

#ifndef FOFX_RECYCLEONDELETE
#define FOFX_RECYCLEONDELETE 0x00080000
#endif
/// TSF_DELETE_RECYCLE_IF_POSSIBLE: set on an item the Recycle Bin will take.
constexpr DWORD kRecyclable = 0x00000080;

/// Lets an item through only when it goes to the Recycle Bin, and notes where
/// it went. Anything else would be deleted for good, so it is stopped.
class RecycleSink final : public IFileOperationProgressSink {
public:
  fs::path recycled;

  // IUnknown, on the stack: the count is kept but never frees.
  IFACEMETHODIMP QueryInterface(REFIID riid, void **out) override {
    if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
      *out = static_cast<IFileOperationProgressSink *>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  IFACEMETHODIMP_(ULONG) Release() override { return --refs_; }

  IFACEMETHODIMP PreDeleteItem(DWORD flags, IShellItem *) override {
    return (flags & kRecyclable) != 0 ? S_OK : E_ABORT;
  }
  IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem *, HRESULT result,
                                IShellItem *created) override {
    if (SUCCEEDED(result) && created != nullptr) {
      PWSTR path = nullptr;
      if (SUCCEEDED(created->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        recycled = path;
        CoTaskMemFree(path);
      }
    }
    return S_OK;
  }

  IFACEMETHODIMP StartOperations() override { return S_OK; }
  IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
  IFACEMETHODIMP PreRenameItem(DWORD, IShellItem *, LPCWSTR) override {
    return S_OK;
  }
  IFACEMETHODIMP PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT,
                                IShellItem *) override {
    return S_OK;
  }
  IFACEMETHODIMP PreMoveItem(DWORD, IShellItem *, IShellItem *,
                             LPCWSTR) override {
    return S_OK;
  }
  IFACEMETHODIMP PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR,
                              HRESULT, IShellItem *) override {
    return S_OK;
  }
  IFACEMETHODIMP PreCopyItem(DWORD, IShellItem *, IShellItem *,
                             LPCWSTR) override {
    return S_OK;
  }
  IFACEMETHODIMP PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR,
                              HRESULT, IShellItem *) override {
    return S_OK;
  }
  IFACEMETHODIMP PreNewItem(DWORD, IShellItem *, LPCWSTR) override {
    return S_OK;
  }
  IFACEMETHODIMP PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD,
                             HRESULT, IShellItem *) override {
    return S_OK;
  }
  IFACEMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
  IFACEMETHODIMP ResetTimer() override { return S_OK; }
  IFACEMETHODIMP PauseTimer() override { return S_OK; }
  IFACEMETHODIMP ResumeTimer() override { return S_OK; }

private:
  ULONG refs_ = 1;
};

/// COM for the calling thread, for as long as this lives.
class ComScope {
public:
  ComScope()
      : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                            COINIT_DISABLE_OLE1DDE)) {}
  ~ComScope() {
    if (SUCCEEDED(result_)) {
      CoUninitialize();
    }
  }
  /// IFileOperation needs a single-threaded apartment.
  bool ready() const { return SUCCEEDED(result_); }

private:
  HRESULT result_;
};

template <typename T> void release(T *&p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

} // namespace

bool available() { return true; }

const char *name() { return "Recycle Bin"; }

std::optional<fs::path> move_to_trash(const fs::path &path) {
  ComScope com;
  if (!com.ready()) {
    return std::nullopt;
  }
  IFileOperation *operation = nullptr;
  IShellItem *item = nullptr;
  RecycleSink sink;
  bool done = false;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                 IID_PPV_ARGS(&operation))) &&
      SUCCEEDED(operation->SetOperationFlags(
          FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT |
          FOFX_EARLYFAILURE | FOFX_RECYCLEONDELETE)) &&
      SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                            IID_PPV_ARGS(&item))) &&
      SUCCEEDED(operation->DeleteItem(item, &sink)) &&
      SUCCEEDED(operation->PerformOperations())) {
    BOOL aborted = FALSE;
    done = SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && !aborted;
  }
  release(item);
  release(operation);
  std::error_code ec;
  if (!done || fs::exists(path, ec)) {
    return std::nullopt;
  }
  return sink.recycled;
}

bool put_back(const fs::path &in_trash, const fs::path &path) {
  std::error_code ec;
  fs::rename(in_trash, path, ec);
  if (ec) {
    return false;
  }
  // The Recycle Bin keeps the item as $R... and what it knows of it as $I...
  const std::wstring name = in_trash.filename().wstring();
  if (name.size() > 2 && name.compare(0, 2, L"$R") == 0) {
    fs::remove(in_trash.parent_path() / (L"$I" + name.substr(2)), ec);
  }
  return true;
}

} // namespace platform::trash
