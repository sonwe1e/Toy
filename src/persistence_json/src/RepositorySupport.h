#pragma once

#include "dvs/application/Ports.h"
#include "dvs/platform/PlatformResult.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace dvs::persistence::internal {

[[nodiscard]] inline domain::MediaError persistenceError(const domain::MediaErrorCode code,
                                                         std::optional<domain::SourceId> sourceId,
                                                         std::string technicalDetail) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kProjectPersistence,
                                  sourceId,
                                  false,
                                  std::move(technicalDetail));
}

[[nodiscard]] inline domain::MediaError platformError(const platform::PlatformError& error,
                                                      std::optional<domain::SourceId> sourceId) {
    return persistenceError(domain::MediaErrorCode::kProjectFileIo,
                            sourceId,
                            std::string{"Platform error "} +
                                std::string{platform::stableId(error.code)} + ": " +
                                error.technicalDetail);
}

[[nodiscard]] inline const application::RequestContext&
requestContextFor(const application::RequestContext& context) noexcept {
    return context;
}

[[nodiscard]] inline const application::RequestContext&
requestContextFor(const application::SaveRequestContext& context) noexcept {
    return context.request;
}

template <typename TContext>
[[nodiscard]] domain::MediaError withRequestId(domain::MediaError error,
                                               const TContext& context) noexcept {
    static_assert(std::is_same_v<std::remove_cvref_t<TContext>, application::RequestContext> ||
                  std::is_same_v<std::remove_cvref_t<TContext>, application::SaveRequestContext>);
    error.requestId = requestContextFor(context).requestId;
    return error;
}

class OperationState final {
public:
    explicit OperationState(application::RequestContext context) noexcept : context_(context) {}

    [[nodiscard]] const application::RequestContext& context() const noexcept {
        return context_;
    }

    // A save claims this state immediately before its atomic publication. Cancellation and that
    // claim race through one compare/exchange so a durable save can never later report Canceled.
    void requestCancellation() noexcept {
        CommitState expected = CommitState::kPending;
        static_cast<void>(commitState_.compare_exchange_strong(expected,
                                                               CommitState::kCanceled,
                                                               std::memory_order_acq_rel,
                                                               std::memory_order_acquire));
    }

    [[nodiscard]] bool isCanceled() const noexcept {
        return commitState_.load(std::memory_order_acquire) == CommitState::kCanceled;
    }

    // Returns false only when cancellation won before a save reached its publication boundary.
    [[nodiscard]] bool tryBeginCommit() noexcept {
        CommitState expected = CommitState::kPending;
        return commitState_.compare_exchange_strong(expected,
                                                    CommitState::kCommitClaimed,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire);
    }

    [[nodiscard]] bool claimTerminal() noexcept {
        bool expected = false;
        return terminalClaimed_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

private:
    enum class CommitState {
        kPending,
        kCanceled,
        kCommitClaimed,
    };

    application::RequestContext context_;
    std::atomic<CommitState> commitState_ = CommitState::kPending;
    std::atomic<bool> terminalClaimed_ = false;
};

class OperationRegistry final {
public:
    [[nodiscard]] std::shared_ptr<OperationState> add(const application::RequestContext& context) {
        auto operation = std::make_shared<OperationState>(context);
        std::scoped_lock lock(mutex_);
        operations_.push_back(operation);
        return operation;
    }

    void remove(const std::shared_ptr<OperationState>& operation) noexcept {
        std::scoped_lock lock(mutex_);
        const auto iterator = std::remove(operations_.begin(), operations_.end(), operation);
        operations_.erase(iterator, operations_.end());
    }

    void cancel(const application::RequestContext& context) noexcept {
        std::scoped_lock lock(mutex_);
        for (const std::shared_ptr<OperationState>& operation : operations_) {
            if (operation->context() == context) {
                operation->requestCancellation();
            }
        }
    }

    void cancelAll() noexcept {
        std::scoped_lock lock(mutex_);
        for (const std::shared_ptr<OperationState>& operation : operations_) {
            operation->requestCancellation();
        }
    }

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<OperationState>> operations_;
};

inline void postCritical(const std::weak_ptr<application::IApplicationEventSink>& events,
                         application::ApplicationEvent event) noexcept {
    if (const std::shared_ptr<application::IApplicationEventSink> sink = events.lock()) {
        static_cast<void>(sink->postCritical(std::move(event)));
    }
}

template <typename TContext>
void postSucceeded(const std::weak_ptr<application::IApplicationEventSink>& events,
                   TContext context) noexcept {
    application::EventContext eventContext{std::move(context)};
    application::ApplicationEvent event{application::RequestSucceeded{
        .context = std::move(eventContext),
    }};
    postCritical(events, std::move(event));
}

template <typename TContext>
void postFailed(const std::weak_ptr<application::IApplicationEventSink>& events,
                TContext context,
                domain::MediaError error) noexcept {
    error = withRequestId(std::move(error), context);
    application::EventContext eventContext{std::move(context)};
    application::ApplicationEvent event{application::RequestFailed{
        .context = std::move(eventContext),
        .error = std::move(error),
    }};
    postCritical(events, std::move(event));
}

template <typename TContext>
void postCanceled(const std::weak_ptr<application::IApplicationEventSink>& events,
                  TContext context) noexcept {
    application::EventContext eventContext{std::move(context)};
    application::ApplicationEvent event{application::RequestCanceled{
        .context = std::move(eventContext),
        .reason = application::CancellationReason::UserRequested,
    }};
    postCritical(events, std::move(event));
}

template <typename TContext>
void completeCanceled(const std::shared_ptr<OperationState>& operation,
                      const std::weak_ptr<application::IApplicationEventSink>& events,
                      TContext context) noexcept {
    if (operation->claimTerminal()) {
        postCanceled(events, std::move(context));
    }
}

template <typename TContext>
void completeFailed(const std::shared_ptr<OperationState>& operation,
                    const std::weak_ptr<application::IApplicationEventSink>& events,
                    TContext context,
                    domain::MediaError error) noexcept {
    if (operation->isCanceled()) {
        completeCanceled(operation, events, std::move(context));
        return;
    }
    if (operation->claimTerminal()) {
        postFailed(events, std::move(context), std::move(error));
    }
}

} // namespace dvs::persistence::internal
