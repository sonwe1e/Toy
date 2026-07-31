#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <span>
#include <vector>
#include <windows.h>

namespace {

[[nodiscard]] bool writeAll(HANDLE file, const std::span<const std::byte> bytes) {
    std::size_t written = 0U;
    while (written < bytes.size()) {
        DWORD chunk = 0U;
        const DWORD remaining = static_cast<DWORD>(bytes.size() - written);
        if (WriteFile(file, bytes.data() + written, remaining, &chunk, nullptr) == FALSE ||
            chunk == 0U) {
            return false;
        }
        written += chunk;
    }
    return true;
}

} // namespace

int wmain(const int argumentCount, wchar_t** const arguments) {
    std::vector<wchar_t> capturePath(32768U, L'\0');
    const DWORD pathLength = GetEnvironmentVariableW(
        L"DVS_SHELL_TEST_CAPTURE_FILE", capturePath.data(), static_cast<DWORD>(capturePath.size()));
    if (pathLength == 0U || pathLength >= capturePath.size()) {
        return 2;
    }
    capturePath.resize(pathLength);
    const HANDLE file = CreateFileW(capturePath.data(),
                                    GENERIC_WRITE,
                                    0U,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 3;
    }

    const std::uint32_t count = static_cast<std::uint32_t>(argumentCount);
    bool succeeded = writeAll(
        file, std::as_bytes(std::span<const std::uint32_t>{&count, static_cast<std::size_t>(1U)}));
    for (int index = 0; succeeded && index < argumentCount; ++index) {
        const std::span<const wchar_t> argument{arguments[index], wcslen(arguments[index])};
        const std::uint32_t length = static_cast<std::uint32_t>(argument.size());
        succeeded = writeAll(file,
                             std::as_bytes(std::span<const std::uint32_t>{
                                 &length,
                                 static_cast<std::size_t>(1U),
                             })) &&
                    writeAll(file, std::as_bytes(argument));
    }
    CloseHandle(file);
    return succeeded ? 0 : 4;
}
