#pragma once

#include <string_view>

namespace dvs::persistence {

[[nodiscard]] std::string_view moduleName() noexcept;

} // namespace dvs::persistence
