#include "ExplorerCommandSupport.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <filesystem>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <vector>
// Windows base declarations must precede shellapi.h.
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

namespace dvs::shell {
namespace {

[[nodiscard]] std::vector<std::wstring> parseCommandLine(const std::wstring& commandLine) {
    int count = 0;
    LPWSTR* const raw = CommandLineToArgvW(commandLine.c_str(), &count);
    if (raw == nullptr) {
        return {};
    }
    std::vector<std::wstring> arguments;
    arguments.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        arguments.emplace_back(raw[index]);
    }
    LocalFree(raw);
    return arguments;
}

TEST(ExplorerCommandSupportTests, AcceptsSupportedVideoExtensionsCaseInsensitively) {
    EXPECT_TRUE(hasSupportedVideoExtension(L"C:\\clips\\review.MP4"));
    EXPECT_TRUE(hasSupportedVideoExtension(L"C:\\clips\\review.mKv"));
    EXPECT_TRUE(hasSupportedVideoExtension(L"C:\\clips\\review.mov"));
    EXPECT_TRUE(hasSupportedVideoExtension(L"C:\\clips\\review.avi"));
    EXPECT_TRUE(hasSupportedVideoExtension(L"C:\\clips\\review.m4v"));
    EXPECT_FALSE(hasSupportedVideoExtension(L"C:\\clips\\review.txt"));
}

TEST(ExplorerCommandSupportTests, BuildsUnicodeCompareCommandWithWindowsRoundTripQuoting) {
    const std::filesystem::path executable = LR"(C:\Program Files\VCStation\VCStation.exe)";
    const std::array<std::filesystem::path, 2U> sources{
        std::filesystem::path{LR"(C:\素材\甲 视频.mp4)"},
        std::filesystem::path{LR"(D:\素材\乙 视频.mkv)"},
    };
    const std::wstring commandLine = buildCompareCommandLine(executable, sources);
    const std::vector<std::wstring> arguments = parseCommandLine(commandLine);
    ASSERT_EQ(arguments.size(), 4U);
    EXPECT_EQ(arguments[0], executable.wstring());
    EXPECT_EQ(arguments[1], L"--compare");
    EXPECT_EQ(arguments[2], sources[0].wstring());
    EXPECT_EQ(arguments[3], sources[1].wstring());
}

TEST(ExplorerCommandSupportTests, RejectsCompareCommandWithAnyCountOtherThanTwo) {
    const std::filesystem::path executable = LR"(C:\VCStation\VCStation.exe)";
    const std::array<std::filesystem::path, 1U> oneSource{
        std::filesystem::path{LR"(C:\clips\a.mp4)"},
    };
    EXPECT_TRUE(buildCompareCommandLine(executable, oneSource).empty());
    EXPECT_TRUE(
        buildCompareCommandLine(executable, std::span<const std::filesystem::path>{}).empty());
}

} // namespace
} // namespace dvs::shell
