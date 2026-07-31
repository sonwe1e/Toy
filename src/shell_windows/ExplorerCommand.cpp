#include "ExplorerCommand.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <string_view>
#include <vector>

namespace dvs::shell {
namespace {

constexpr std::array<std::wstring_view, 5U> kSupportedExtensions{
    L".mp4",
    L".mkv",
    L".mov",
    L".avi",
    L".m4v",
};

[[nodiscard]] bool supportedExtension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    for (wchar_t& character : extension) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    return std::ranges::find(kSupportedExtensions, extension) != kSupportedExtensions.end();
}

[[nodiscard]] std::vector<std::filesystem::path> selectedPaths(IShellItemArray* selection) {
    std::vector<std::filesystem::path> paths;
    if (selection == nullptr) {
        return paths;
    }
    DWORD count = 0U;
    if (FAILED(selection->GetCount(&count)) || count != 2U) {
        return paths;
    }
    paths.reserve(2U);
    for (DWORD index = 0U; index < count; ++index) {
        IShellItem* item = nullptr;
        if (FAILED(selection->GetItemAt(index, &item)) || item == nullptr) {
            paths.clear();
            return paths;
        }
        PWSTR rawPath = nullptr;
        const HRESULT displayResult = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
        item->Release();
        if (FAILED(displayResult) || rawPath == nullptr) {
            CoTaskMemFree(rawPath);
            paths.clear();
            return paths;
        }
        std::filesystem::path path{rawPath};
        CoTaskMemFree(rawPath);
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
            PathIsNetworkPathW(path.c_str()) != FALSE || !supportedExtension(path)) {
            paths.clear();
            return paths;
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

[[nodiscard]] std::wstring quoteArgument(const std::wstring_view argument) {
    std::wstring quoted;
    quoted.push_back(L'"');
    std::size_t slashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(slashes * 2U + 1U, L'\\');
            quoted.push_back(L'"');
            slashes = 0U;
            continue;
        }
        quoted.append(slashes, L'\\');
        slashes = 0U;
        quoted.push_back(character);
    }
    quoted.append(slashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

[[nodiscard]] std::filesystem::path executablePath() {
    std::wstring modulePath(32768U, L'\0');
    const DWORD length =
        GetModuleFileNameW(gModule, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0U || length >= modulePath.size()) {
        return {};
    }
    modulePath.resize(length);
    return std::filesystem::path{modulePath}.parent_path() / L"VCStation.exe";
}

} // namespace

ExplorerCommand::ExplorerCommand() noexcept {
    gModuleReferences.fetch_add(1U, std::memory_order_relaxed);
}

ExplorerCommand::~ExplorerCommand() {
    gModuleReferences.fetch_sub(1U, std::memory_order_relaxed);
}

HRESULT ExplorerCommand::QueryInterface(REFIID interfaceId, void** const object) noexcept {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (interfaceId == IID_IUnknown || interfaceId == IID_IExplorerCommand) {
        *object = static_cast<IExplorerCommand*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG ExplorerCommand::AddRef() noexcept {
    return references_.fetch_add(1U, std::memory_order_relaxed) + 1U;
}

ULONG ExplorerCommand::Release() noexcept {
    const ULONG remaining = references_.fetch_sub(1U, std::memory_order_acq_rel) - 1U;
    if (remaining == 0U) {
        delete this;
    }
    return remaining;
}

HRESULT ExplorerCommand::GetTitle(IShellItemArray*, LPWSTR* const title) noexcept {
    return title == nullptr ? E_POINTER : SHStrDupW(L"使用 VCStation 对比", title);
}

HRESULT ExplorerCommand::GetIcon(IShellItemArray*, LPWSTR* const icon) noexcept {
    if (icon == nullptr) {
        return E_POINTER;
    }
    try {
        const std::wstring value = executablePath().wstring() + L",0";
        return SHStrDupW(value.c_str(), icon);
    } catch (...) {
        *icon = nullptr;
        return E_OUTOFMEMORY;
    }
}

HRESULT ExplorerCommand::GetToolTip(IShellItemArray*, LPWSTR* const toolTip) noexcept {
    return toolTip == nullptr ? E_POINTER
                              : SHStrDupW(L"在 VCStation 中逐帧对比选中的两个视频", toolTip);
}

HRESULT ExplorerCommand::GetCanonicalName(GUID* const commandName) noexcept {
    if (commandName == nullptr) {
        return E_POINTER;
    }
    *commandName = kExplorerCommandClsid;
    return S_OK;
}

HRESULT ExplorerCommand::GetState(IShellItemArray* const selection,
                                  BOOL,
                                  EXPCMDSTATE* const state) noexcept {
    if (state == nullptr) {
        return E_POINTER;
    }
    try {
        *state = selectedPaths(selection).size() == 2U ? ECS_ENABLED : ECS_HIDDEN;
    } catch (...) {
        *state = ECS_HIDDEN;
    }
    return S_OK;
}

HRESULT ExplorerCommand::Invoke(IShellItemArray* const selection, IBindCtx*) noexcept {
    try {
        const std::vector<std::filesystem::path> paths = selectedPaths(selection);
        if (paths.size() != 2U) {
            return E_INVALIDARG;
        }
        const std::filesystem::path executable = executablePath();
        if (executable.empty()) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        std::wstring commandLine = quoteArgument(executable.wstring()) + L" --compare " +
                                   quoteArgument(paths[0].wstring()) + L" " +
                                   quoteArgument(paths[1].wstring());
        STARTUPINFOW startupInfo{
            .cb = sizeof(STARTUPINFOW),
        };
        PROCESS_INFORMATION processInformation{};
        if (CreateProcessW(executable.c_str(),
                           commandLine.data(),
                           nullptr,
                           nullptr,
                           FALSE,
                           0U,
                           nullptr,
                           executable.parent_path().c_str(),
                           &startupInfo,
                           &processInformation) == FALSE) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

HRESULT ExplorerCommand::GetFlags(EXPCMDFLAGS* const flags) noexcept {
    if (flags == nullptr) {
        return E_POINTER;
    }
    *flags = ECF_DEFAULT;
    return S_OK;
}

HRESULT ExplorerCommand::EnumSubCommands(IEnumExplorerCommand** const commands) noexcept {
    if (commands == nullptr) {
        return E_POINTER;
    }
    *commands = nullptr;
    return E_NOTIMPL;
}

} // namespace dvs::shell
