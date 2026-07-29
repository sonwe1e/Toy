#include "dvs/application/Module.h"

#include "dvs/domain/Module.h"

namespace dvs::application {

std::string_view moduleName() noexcept {
    if (domain::moduleName() != "dvs_domain") {
        return {};
    }
    return "dvs_application";
}

} // namespace dvs::application
