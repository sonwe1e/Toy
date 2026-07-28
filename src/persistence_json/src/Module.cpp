#include "dvs/persistence/Module.h"

#include "dvs/application/Module.h"
#include "dvs/platform/Module.h"

namespace dvs::persistence {

std::string_view moduleName() noexcept {
    if (application::moduleName() != "dvs_application" ||
        platform::moduleName() != "dvs_platform_windows") {
        return {};
    }
    return "dvs_persistence_json";
}

} // namespace dvs::persistence
