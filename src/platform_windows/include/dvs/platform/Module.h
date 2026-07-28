#pragma once

#include <string_view>

namespace dvs::platform {

[[nodiscard]] std::string_view moduleName() noexcept;

} // namespace dvs::platform
