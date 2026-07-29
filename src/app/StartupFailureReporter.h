#pragma once

#include <string_view>

namespace dvs::app {

[[nodiscard]] int reportFatalStartup(std::string_view technicalDetail,
                                     bool suppressDialog) noexcept;

} // namespace dvs::app
