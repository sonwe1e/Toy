#include "dvs/ui/ReviewPreferencesController.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace dvs::ui {
namespace {

using namespace std::chrono_literals;

void ensurePreferencesCoreApplication() {
    if (QCoreApplication::instance() != nullptr) {
        return;
    }
    static int argumentCount = 1;
    static char applicationName[] = "ReviewPreferencesControllerTests";
    static char* arguments[] = {applicationName, nullptr};
    static QCoreApplication application{argumentCount, arguments};
    static_cast<void>(application);
}

template <typename Predicate>
[[nodiscard]] bool waitForPreferences(Predicate predicate,
                                      const std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1U);
    }
    return predicate();
}

class FakeSettingsRepository final : public application::ISettingsRepository {
public:
    [[nodiscard]] application::PortSubmitResult
    submit(const application::SettingsLoadRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override {
        loadRequests.push_back(request);
        loadEvents = std::move(events);
        return loadSubmitResult;
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::SettingsSaveRequest& request,
           std::shared_ptr<application::IApplicationEventSink> events) override {
        saveRequests.push_back(request);
        saveEvents = std::move(events);
        if (autoCompleteSaves && saveSubmitResult == application::PortSubmitResult::Accepted) {
            postTerminal(*saveEvents, request.context, true);
        }
        return saveSubmitResult;
    }

    void cancel(const application::RequestContext& context) noexcept override {
        canceled.push_back(context);
    }

    void completeLoad(application::SettingsSnapshot settings, const bool succeeded = true) {
        ASSERT_NE(loadEvents, nullptr);
        ASSERT_FALSE(loadRequests.empty());
        const application::RequestContext context = loadRequests.back().context;
        if (succeeded) {
            EXPECT_EQ(
                loadEvents->postCritical(application::ApplicationEvent{application::SettingsLoaded{
                    .context = context,
                    .settings = std::move(settings),
                }}),
                application::EventPostResult::Accepted);
        }
        postTerminal(*loadEvents, context, succeeded);
    }

    void completeSave(const bool succeeded = true) {
        ASSERT_NE(saveEvents, nullptr);
        ASSERT_FALSE(saveRequests.empty());
        postTerminal(*saveEvents, saveRequests.back().context, succeeded);
    }

    application::PortSubmitResult loadSubmitResult = application::PortSubmitResult::Accepted;
    application::PortSubmitResult saveSubmitResult = application::PortSubmitResult::Accepted;
    bool autoCompleteSaves = false;
    std::vector<application::SettingsLoadRequest> loadRequests;
    std::vector<application::SettingsSaveRequest> saveRequests;
    std::vector<application::RequestContext> canceled;
    std::shared_ptr<application::IApplicationEventSink> loadEvents;
    std::shared_ptr<application::IApplicationEventSink> saveEvents;

private:
    static void postTerminal(application::IApplicationEventSink& events,
                             const application::RequestContext& context,
                             const bool succeeded) {
        if (succeeded) {
            EXPECT_EQ(events.postCritical(application::ApplicationEvent{
                          application::RequestTerminal{application::RequestSucceeded{
                              .context = application::EventContext{context},
                          }}}),
                      application::EventPostResult::Accepted);
        } else {
            EXPECT_EQ(
                events.postCritical(application::ApplicationEvent{
                    application::RequestTerminal{application::RequestFailed{
                        .context = application::EventContext{context},
                        .error = domain::makeMediaError(domain::MediaErrorCode::kProjectFileIo,
                                                        domain::MediaOperation::kProjectPersistence,
                                                        domain::SourceRole::kProject,
                                                        true),
                    }}}),
                application::EventPostResult::Accepted);
        }
    }
};

class ReviewPreferencesControllerTests : public ::testing::Test {
protected:
    void SetUp() override {
        ensurePreferencesCoreApplication();
    }
};

TEST_F(ReviewPreferencesControllerTests, DefaultsThenProjectsValidPersistedValues) {
    auto repository = std::make_shared<FakeSettingsRepository>();
    ReviewPreferencesController controller{repository};
    ASSERT_EQ(repository->loadRequests.size(), 1U);
    EXPECT_EQ(controller.largeStepFrames(), 10);
    EXPECT_EQ(controller.viewMode(), ReviewPreferencesController::ViewMode::SideBySide);
    EXPECT_EQ(controller.differenceFilter(),
              ReviewPreferencesController::DifferenceFilter::Bilinear);

    application::SettingsSnapshot settings;
    settings.values.emplace("review.large-step-frames", "5");
    settings.values.emplace("review.view-mode", "difference");
    settings.values.emplace("review.difference-metric", "heatmap");
    settings.values.emplace("review.difference-gain", "8x");
    settings.values.emplace("review.difference-reference", "b");
    settings.values.emplace("review.difference-filter", "bicubic");
    repository->completeLoad(std::move(settings));

    ASSERT_TRUE(waitForPreferences([&controller] { return controller.largeStepFrames() == 5; }));
    EXPECT_EQ(controller.viewMode(), ReviewPreferencesController::ViewMode::Difference);
    EXPECT_EQ(controller.differenceMetric(),
              ReviewPreferencesController::DifferenceMetric::Heatmap);
    EXPECT_EQ(controller.differenceGain(), ReviewPreferencesController::DifferenceGain::Gain8x);
    EXPECT_EQ(controller.differenceReference(),
              ReviewPreferencesController::DifferenceReference::ReferenceB);
    EXPECT_EQ(controller.differenceFilter(),
              ReviewPreferencesController::DifferenceFilter::Bicubic);
    controller.stop();
}

TEST_F(ReviewPreferencesControllerTests, InvalidValuesFallBackWithoutBlockingStartup) {
    auto repository = std::make_shared<FakeSettingsRepository>();
    ReviewPreferencesController controller{repository};
    application::SettingsSnapshot settings;
    settings.values.emplace("review.large-step-frames", "7");
    settings.values.emplace("review.view-mode", "unknown");
    settings.values.emplace("review.difference-metric", "ssim");
    settings.values.emplace("review.difference-gain", "100x");
    settings.values.emplace("review.difference-reference", "both");
    settings.values.emplace("review.difference-filter", "lanczos");
    repository->completeLoad(std::move(settings));

    ASSERT_TRUE(waitForPreferences([&repository] { return repository->loadEvents != nullptr; }));
    EXPECT_EQ(controller.largeStepFrames(), 10);
    EXPECT_EQ(controller.viewMode(), ReviewPreferencesController::ViewMode::SideBySide);
    EXPECT_EQ(controller.differenceMetric(),
              ReviewPreferencesController::DifferenceMetric::RgbAbsolute);
    EXPECT_EQ(controller.differenceGain(), ReviewPreferencesController::DifferenceGain::Gain1x);
    EXPECT_EQ(controller.differenceReference(),
              ReviewPreferencesController::DifferenceReference::ReferenceA);
    EXPECT_EQ(controller.differenceFilter(),
              ReviewPreferencesController::DifferenceFilter::Bilinear);
    controller.stop();
}

TEST_F(ReviewPreferencesControllerTests, CoalescesChangesAndPreservesUnknownSettings) {
    auto repository = std::make_shared<FakeSettingsRepository>();
    ReviewPreferencesController controller{repository};
    application::SettingsSnapshot settings;
    settings.values.emplace("future.setting", "keep-me");
    repository->completeLoad(std::move(settings));
    ASSERT_TRUE(waitForPreferences([&controller] {
        return controller.viewMode() == ReviewPreferencesController::ViewMode::SideBySide;
    }));

    controller.setLargeStepFrames(5);
    controller.setViewMode(ReviewPreferencesController::ViewMode::Difference);
    controller.setDifferenceMetric(ReviewPreferencesController::DifferenceMetric::Chroma);
    controller.setDifferenceGain(ReviewPreferencesController::DifferenceGain::Gain16x);
    controller.setDifferenceReference(ReviewPreferencesController::DifferenceReference::ReferenceB);
    controller.setDifferenceFilter(ReviewPreferencesController::DifferenceFilter::Nearest);

    ASSERT_TRUE(
        waitForPreferences([&repository] { return repository->saveRequests.size() == 1U; }));
    const auto& values = repository->saveRequests.front().settings.values;
    EXPECT_EQ(values.at("future.setting"), "keep-me");
    EXPECT_EQ(values.at("review.large-step-frames"), "5");
    EXPECT_EQ(values.at("review.view-mode"), "difference");
    EXPECT_EQ(values.at("review.difference-metric"), "chroma");
    EXPECT_EQ(values.at("review.difference-gain"), "16x");
    EXPECT_EQ(values.at("review.difference-reference"), "b");
    EXPECT_EQ(values.at("review.difference-filter"), "nearest");
    repository->completeSave();
    controller.stop();
}

TEST_F(ReviewPreferencesControllerTests, RejectsUnsupportedSetterValuesAndCancelsPendingWork) {
    auto repository = std::make_shared<FakeSettingsRepository>();
    ReviewPreferencesController controller{repository};
    controller.setLargeStepFrames(7);
    EXPECT_EQ(controller.largeStepFrames(), 10);
    controller.stop();
    ASSERT_EQ(repository->canceled.size(), 1U);
    EXPECT_EQ(repository->canceled.front(), repository->loadRequests.front().context);
}

TEST_F(ReviewPreferencesControllerTests, FlushesLatestChangeAndClosesLateEventsDuringStop) {
    auto repository = std::make_shared<FakeSettingsRepository>();
    repository->autoCompleteSaves = true;
    ReviewPreferencesController controller{repository};

    application::SettingsSnapshot settings;
    settings.values.emplace("future.setting", "keep-me");
    repository->completeLoad(std::move(settings));
    controller.setViewMode(ReviewPreferencesController::ViewMode::Difference);

    controller.stop();

    ASSERT_EQ(repository->saveRequests.size(), 1U);
    const auto& values = repository->saveRequests.front().settings.values;
    EXPECT_EQ(values.at("future.setting"), "keep-me");
    EXPECT_EQ(values.at("review.view-mode"), "difference");
    ASSERT_NE(repository->saveEvents, nullptr);
    EXPECT_EQ(
        repository->saveEvents->postCritical(application::ApplicationEvent{
            application::RequestTerminal{application::RequestSucceeded{
                .context = application::EventContext{repository->saveRequests.front().context},
            }}}),
        application::EventPostResult::Closed);
}

} // namespace
} // namespace dvs::ui
