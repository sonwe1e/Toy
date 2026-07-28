#pragma once

#include <string_view>

namespace dvs::domain {

[[nodiscard]] std::string_view moduleName() noexcept;

} // namespace dvs::domain
