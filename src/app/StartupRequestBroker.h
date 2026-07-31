#pragma once

#include "StartupRequest.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace dvs::app {

class StartupRequestBroker final : public QObject {
public:
    enum class StartResult {
        Primary,
        Forwarded,
        Failed,
    };

    explicit StartupRequestBroker(QObject* parent = nullptr);
    explicit StartupRequestBroker(QString endpointName, QObject* parent = nullptr);
    ~StartupRequestBroker() override;

    StartupRequestBroker(const StartupRequestBroker&) = delete;
    StartupRequestBroker& operator=(const StartupRequestBroker&) = delete;

    [[nodiscard]] StartResult startOrForward(const StartupRequest& request);
    void setRequestHandler(std::function<void(StartupRequest)> handler);

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::app
