#include "dvs/platform/ProcessRunner.h"

#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace dvs::platform {

TEST(ProcessRunnerTests, QuotesWindowsArgumentsWithoutUsingAShell) {
    EXPECT_EQ(ProcessRunner::quoteWindowsArgument(L"plain"), L"plain");
    EXPECT_EQ(ProcessRunner::quoteWindowsArgument(L"two words"), L"\"two words\"");
    EXPECT_EQ(ProcessRunner::quoteWindowsArgument(L"a\"b"), L"\"a\\\"b\"");
    EXPECT_EQ(ProcessRunner::quoteWindowsArgument(L"C:\\path with spaces\\"),
              L"\"C:\\path with spaces\\\\\"");
}

TEST(ProcessRunnerTests, CapturesBoundedOutputFromAnExecutable) {
    ProcessRunOptions options{
        .maxStandardOutputBytes = 1,
        .maxStandardErrorBytes = 1024,
        .cancellationToken = CancellationToken{},
    };

    const ProcessRunResult result = ProcessRunner::run(
        std::vector<std::wstring>{L"C:\\Windows\\System32\\whoami.exe"}, options);

    ASSERT_EQ(result.status, ProcessRunStatus::kExited);
    ASSERT_TRUE(result.exitCode.has_value());
    EXPECT_EQ(*result.exitCode, 0U);
    EXPECT_FALSE(result.standardOutput.empty());
    EXPECT_TRUE(result.standardOutputTruncated);
    EXPECT_FALSE(result.error.has_value());
}

TEST(ProcessRunnerTests, CancelsLongRunningChildrenThroughTheJobObject) {
    using namespace std::chrono_literals;

    CancellationToken cancellation;
    const ProcessRunOptions options{
        .maxStandardOutputBytes = 1024,
        .maxStandardErrorBytes = 1024,
        .cancellationToken = cancellation,
    };
    ProcessRunResult result;
    std::thread runner{[&]() {
        result = ProcessRunner::run(
            std::vector<std::wstring>{
                L"C:\\Windows\\System32\\ping.exe",
                L"127.0.0.1",
                L"-t",
            },
            options);
    }};

    std::this_thread::sleep_for(100ms);
    cancellation.requestCancellation();
    runner.join();

    EXPECT_EQ(result.status, ProcessRunStatus::kCanceled);
    EXPECT_TRUE(result.forceTerminated);
    EXPECT_FALSE(result.error.has_value());
}

TEST(ProcessRunnerTests, CancelsContinuouslyChattyChildrenWithoutOutputStarvation) {
    using namespace std::chrono_literals;

    CancellationToken cancellation;
    const ProcessRunOptions options{
        .maxStandardOutputBytes = 64,
        .maxStandardErrorBytes = 64,
        .cancellationToken = cancellation,
    };
    ProcessRunResult result;
    const auto startedAt = std::chrono::steady_clock::now();
    std::thread runner{[&]() {
        result = ProcessRunner::run(
            std::vector<std::wstring>{
                L"C:\\Windows\\System32\\cmd.exe",
                L"/d",
                L"/c",
                L"for /L %i in (1,1,2147483647) do @echo x",
            },
            options);
    }};

    std::this_thread::sleep_for(100ms);
    cancellation.requestCancellation();
    runner.join();

    EXPECT_EQ(result.status, ProcessRunStatus::kCanceled);
    EXPECT_TRUE(result.forceTerminated);
    EXPECT_TRUE(result.standardOutputTruncated);
    EXPECT_FALSE(result.error.has_value());
    EXPECT_LT(std::chrono::steady_clock::now() - startedAt, 5s);
}

} // namespace dvs::platform
