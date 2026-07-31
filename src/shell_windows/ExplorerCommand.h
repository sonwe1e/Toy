#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <shobjidl.h>

namespace dvs::shell {

inline constexpr CLSID kExplorerCommandClsid{
    0x3b790d74,
    0xe76e,
    0x4f28,
    {0xa5, 0x1d, 0x2a, 0xb8, 0xc6, 0xbd, 0x10, 0x7d},
};

extern HMODULE gModule;
extern std::atomic<unsigned long> gModuleReferences;

class ExplorerCommand final : public IExplorerCommand {
public:
    ExplorerCommand() noexcept;

    IFACEMETHODIMP QueryInterface(REFIID interfaceId, void** object) noexcept override;
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override;
    IFACEMETHODIMP_(ULONG) Release() noexcept override;

    IFACEMETHODIMP GetTitle(IShellItemArray* selection, LPWSTR* title) noexcept override;
    IFACEMETHODIMP GetIcon(IShellItemArray* selection, LPWSTR* icon) noexcept override;
    IFACEMETHODIMP GetToolTip(IShellItemArray* selection, LPWSTR* toolTip) noexcept override;
    IFACEMETHODIMP GetCanonicalName(GUID* commandName) noexcept override;
    IFACEMETHODIMP
    GetState(IShellItemArray* selection, BOOL okToBeSlow, EXPCMDSTATE* state) noexcept override;
    IFACEMETHODIMP Invoke(IShellItemArray* selection, IBindCtx* bindContext) noexcept override;
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) noexcept override;
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) noexcept override;

private:
    ~ExplorerCommand();

    std::atomic<unsigned long> references_{1U};
};

} // namespace dvs::shell
