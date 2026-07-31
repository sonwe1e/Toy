#include "ExplorerCommand.h"

#include <new>

namespace dvs::shell {
namespace {

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() noexcept {
        gModuleReferences.fetch_add(1U, std::memory_order_relaxed);
    }

    IFACEMETHODIMP QueryInterface(REFIID interfaceId, void** const object) noexcept override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (interfaceId == IID_IUnknown || interfaceId == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() noexcept override {
        return references_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    }

    IFACEMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG remaining = references_.fetch_sub(1U, std::memory_order_acq_rel) - 1U;
        if (remaining == 0U) {
            delete this;
        }
        return remaining;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* const outer,
                                  REFIID interfaceId,
                                  void** const object) noexcept override {
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        auto* command = new (std::nothrow) ExplorerCommand{};
        if (command == nullptr) {
            return E_OUTOFMEMORY;
        }
        const HRESULT result = command->QueryInterface(interfaceId, object);
        command->Release();
        return result;
    }

    IFACEMETHODIMP LockServer(const BOOL lock) noexcept override {
        if (lock != FALSE) {
            gModuleReferences.fetch_add(1U, std::memory_order_relaxed);
        } else {
            gModuleReferences.fetch_sub(1U, std::memory_order_relaxed);
        }
        return S_OK;
    }

private:
    ~ClassFactory() {
        gModuleReferences.fetch_sub(1U, std::memory_order_relaxed);
    }

    std::atomic<unsigned long> references_{1U};
};

} // namespace
} // namespace dvs::shell

HMODULE dvs::shell::gModule = nullptr;
std::atomic<unsigned long> dvs::shell::gModuleReferences{0U};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        dvs::shell::gModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return dvs::shell::gModuleReferences.load(std::memory_order_acquire) == 0U ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID classId, REFIID interfaceId, void** const object) {
    if (classId != dvs::shell::kExplorerCommandClsid) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new (std::nothrow) dvs::shell::ClassFactory{};
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT result = factory->QueryInterface(interfaceId, object);
    factory->Release();
    return result;
}
