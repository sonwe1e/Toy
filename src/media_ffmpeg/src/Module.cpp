#include "dvs/media/Module.h"

#include "dvs/application/Module.h"
#include "dvs/platform/Module.h"

namespace dvs::media {

std::string_view moduleName() noexcept {
    if (application::moduleName() != "dvs_application" ||
        platform::moduleName() != "dvs_platform_windows") {
        return {};
    }
    return "dvs_media_ffmpeg";
}

} // namespace dvs::media
